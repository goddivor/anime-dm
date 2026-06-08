//! Application egui type IDM : état, actions partagées et boucle de rendu.
//!
//! Le rendu est réparti par responsabilité dans le dossier `gui/` :
//! - [`crate::gui::menu`]    : barre de menus
//! - [`crate::gui::view`]    : barre d'outils, sidebar, table, barre d'état
//! - [`crate::gui::dialogs`] : fenêtres modales (ajout, à propos, aide, manuel)

use std::path::PathBuf;
use std::sync::mpsc::Receiver;

use eframe::egui;

use crate::gui::i18n::{t, Lang};
use crate::model::{Anime, DownloadItem, DownloadStatus};
use crate::selection;
use crate::worker::{DownloadJob, Worker, WorkerMsg};

/// Thème de l'interface.
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum ThemeMode {
    Dark,
    Light,
    System,
}

/// Critère de classement des fichiers (menu Affichage › Classer les fichiers).
#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) enum SortBy {
    DateAdded,
    Name,
    Size,
    Status,
    TimeLeft,
    Speed,
    LastTry,
    Location,
    Address,
    ParentPage,
}

pub struct App {
    // --- cœur (logique de téléchargement, inchangée) ---
    pub(crate) worker: Worker,
    pub(crate) rx: Receiver<WorkerMsg>,
    pub(crate) downloads: Vec<DownloadItem>,
    pub(crate) next_id: u64,
    pub(crate) out_dir: String,

    // --- dialogue d'ajout ---
    pub(crate) show_add: bool,
    pub(crate) url_input: String,
    pub(crate) loading_anime: bool,
    pub(crate) anime: Option<Anime>,
    pub(crate) load_error: Option<String>,
    pub(crate) selection_input: String,
    pub(crate) selected_player: String,

    // --- état UI / préférences (piloté par les menus) ---
    pub(crate) lang: Lang,
    pub(crate) theme: ThemeMode,
    pub(crate) sort_by: SortBy,
    pub(crate) search_open: bool,
    pub(crate) search_query: String,
    pub(crate) show_categories: bool,
    pub(crate) category_filter: Option<String>,
    pub(crate) speed_limit_enabled: bool,
    pub(crate) notifications_enabled: bool,
    pub(crate) toolbar_big: bool,
    pub(crate) selected: Option<u64>,
    pub(crate) status: String,

    // --- dialogues secondaires ---
    pub(crate) show_about: bool,
    pub(crate) show_help: bool,
    pub(crate) show_manual: bool,
    pub(crate) manual_input: String,
}

impl App {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        cc.egui_ctx.set_theme(egui::ThemePreference::Dark);
        // Agrandit légèrement l'interface par-dessus le scaling écran (réglable par l'utilisateur
        // plus tard via un menu). 1.15 reste lisible sans déborder sur petit écran.
        cc.egui_ctx.set_zoom_factor(1.15);

        // Espacement plus généreux (par défaut egui est très compact) : écart entre menus,
        // hauteur/padding des items, marges des popups — pour un rendu aéré façon IDM.
        cc.egui_ctx.all_styles_mut(|s| {
            s.spacing.item_spacing = egui::vec2(12.0, 8.0);
            s.spacing.button_padding = egui::vec2(12.0, 7.0);
            s.spacing.menu_margin = egui::Margin::same(8);
            s.spacing.menu_spacing = 6.0;
            s.spacing.interact_size.y = 26.0;
        });

        let (tx, rx) = std::sync::mpsc::channel();
        let worker = Worker::new(tx, cc.egui_ctx.clone())
            .expect("initialisation du worker (runtime tokio + client HTTP)");

        Self {
            worker,
            rx,
            downloads: Vec::new(),
            next_id: 1,
            out_dir: default_download_dir(),
            show_add: false,
            url_input: String::new(),
            loading_anime: false,
            anime: None,
            load_error: None,
            selection_input: String::new(),
            selected_player: "LECTEUR myTV".to_string(),
            lang: Lang::Fr,
            theme: ThemeMode::Dark,
            sort_by: SortBy::DateAdded,
            search_open: false,
            search_query: String::new(),
            show_categories: true,
            category_filter: None,
            speed_limit_enabled: false,
            notifications_enabled: true,
            toolbar_big: true,
            selected: None,
            status: String::new(),
            show_about: false,
            show_help: false,
            show_manual: false,
            manual_input: String::new(),
        }
    }

    // ------------------------------------------------------------------
    // Actions partagées (appelées par les menus / dialogues — d'où pub(crate))
    // ------------------------------------------------------------------

    pub(crate) fn apply_theme(&self, ctx: &egui::Context) {
        ctx.set_theme(match self.theme {
            ThemeMode::Dark => egui::ThemePreference::Dark,
            ThemeMode::Light => egui::ThemePreference::Light,
            ThemeMode::System => egui::ThemePreference::System,
        });
    }

    /// Message de feedback affiché dans la barre d'état.
    pub(crate) fn set_status(&mut self, msg: impl Into<String>) {
        self.status = msg.into();
    }

    /// Marque une fonctionnalité comme non encore disponible (coquille de menu).
    pub(crate) fn soon(&mut self, feature: &str) {
        let suffix = t(self.lang, "status.coming_soon");
        self.status = format!("« {feature} » — {suffix}");
    }

    pub(crate) fn remove_completed(&mut self) {
        let before = self.downloads.len();
        self.downloads
            .retain(|d| d.status != DownloadStatus::Completed);
        let removed = before - self.downloads.len();
        self.set_status(format!(
            "{removed} {}",
            t(self.lang, "status.completed_removed")
        ));
    }

    pub(crate) fn remove_selected(&mut self) {
        if let Some(id) = self.selected.take() {
            self.downloads.retain(|d| d.id != id);
        }
    }

    pub(crate) fn sort_downloads(&mut self) {
        match self.sort_by {
            SortBy::DateAdded => self.downloads.sort_by_key(|d| d.id),
            SortBy::Name => self
                .downloads
                .sort_by(|a, b| a.filename.to_lowercase().cmp(&b.filename.to_lowercase())),
            SortBy::Size => self
                .downloads
                .sort_by(|a, b| b.total_secs.total_cmp(&a.total_secs)),
            SortBy::Status => self.downloads.sort_by_key(|d| status_rank(d.status)),
            SortBy::Speed => self
                .downloads
                .sort_by(|a, b| b.progress.total_cmp(&a.progress)),
            // Critères sans donnée disponible pour l'instant : on ne réordonne pas.
            SortBy::TimeLeft
            | SortBy::LastTry
            | SortBy::Location
            | SortBy::Address
            | SortBy::ParentPage => {}
        }
    }

    /// Lance les téléchargements sélectionnés dans le dialogue d'ajout.
    pub(crate) fn launch_selection(&mut self) {
        let Some(anime) = self.anime.clone() else { return };
        let numbers = selection::parse(&self.selection_input, anime.episodes.len());
        let _ = std::fs::create_dir_all(&self.out_dir);

        for n in numbers {
            let Some(ep) = anime
                .episodes
                .iter()
                .find(|e| e.number.round() as i64 == n as i64)
            else {
                continue;
            };
            let id = self.next_id;
            self.next_id += 1;
            let filename = sanitize(&format!("{} - Ep {:03}.mp4", anime.title, n));
            let out_path = PathBuf::from(&self.out_dir).join(&filename);

            self.downloads.push(DownloadItem {
                id,
                filename,
                status: DownloadStatus::Queued,
                progress: -1.0,
                speed: String::new(),
                total_secs: 0.0,
                error: None,
            });
            self.worker.start_download(DownloadJob {
                id,
                episode_url: ep.url.clone(),
                player_name: self.selected_player.clone(),
                out_path,
            });
        }

        self.show_add = false;
        self.anime = None;
        self.url_input.clear();
        self.selection_input.clear();
        self.load_error = None;
    }

    // ------------------------------------------------------------------
    // Boucle interne
    // ------------------------------------------------------------------

    fn drain_messages(&mut self) {
        while let Ok(msg) = self.rx.try_recv() {
            match msg {
                WorkerMsg::AnimeLoaded(res) => {
                    self.loading_anime = false;
                    match res {
                        Ok(a) => {
                            self.anime = Some(a);
                            self.load_error = None;
                        }
                        Err(e) => self.load_error = Some(e),
                    }
                }
                WorkerMsg::Progress {
                    id,
                    status,
                    progress,
                    speed,
                    total_secs,
                } => {
                    if let Some(it) = self.downloads.iter_mut().find(|d| d.id == id) {
                        it.status = status;
                        if let Some(p) = progress {
                            it.progress = p;
                        }
                        if let Some(s) = speed {
                            it.speed = s;
                        }
                        if total_secs > 0.0 {
                            it.total_secs = total_secs;
                        }
                    }
                }
                WorkerMsg::Finished { id, result } => {
                    if let Some(it) = self.downloads.iter_mut().find(|d| d.id == id) {
                        match result {
                            Ok(_) => {
                                it.status = DownloadStatus::Completed;
                                it.progress = 1.0;
                                it.speed.clear();
                            }
                            Err(e) => {
                                it.status = DownloadStatus::Failed;
                                it.error = Some(e);
                            }
                        }
                    }
                }
            }
        }
    }

    fn handle_shortcuts(&mut self, ctx: &egui::Context) {
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::CTRL, egui::Key::F)) {
            self.search_open = !self.search_open;
        }
        if ctx.input_mut(|i| {
            i.consume_key(egui::Modifiers::CTRL | egui::Modifiers::SHIFT, egui::Key::V)
        }) {
            self.soon(t(self.lang, "status.batch_clipboard"));
        }
        if ctx.input(|i| i.key_pressed(egui::Key::F1)) {
            self.show_help = true;
        }
    }

}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.drain_messages();
        let ctx = ui.ctx().clone();
        self.handle_shortcuts(&ctx);

        egui::Panel::top("menubar")
            .exact_size(34.0)
            .show_inside(ui, |ui| self.show_menu_bar(ui));
        egui::Panel::top("toolbar")
            .exact_size(48.0)
            .show_inside(ui, |ui| self.ui_toolbar(ui));
        egui::Panel::bottom("statusbar")
            .exact_size(28.0)
            .show_inside(ui, |ui| self.ui_statusbar(ui));
        if self.show_categories {
            egui::Panel::left("categories")
                .exact_size(170.0)
                .show_inside(ui, |ui| self.ui_sidebar(ui));
        }
        egui::CentralPanel::default().show_inside(ui, |ui| self.ui_table(ui));

        self.ui_add_dialog(&ctx);
        self.ui_simple_dialogs(&ctx);

        let busy = self.loading_anime || self.downloads.iter().any(|d| is_active(d.status));
        if busy {
            ctx.request_repaint_after(std::time::Duration::from_millis(250));
        }
    }
}

// ----------------------------------------------------------------------
// Helpers libres
// ----------------------------------------------------------------------

/// Un téléchargement encore en cours (en attente, résolution ou téléchargement).
pub(crate) fn is_active(s: DownloadStatus) -> bool {
    matches!(
        s,
        DownloadStatus::Downloading | DownloadStatus::Resolving | DownloadStatus::Queued
    )
}

fn status_rank(s: DownloadStatus) -> u8 {
    match s {
        DownloadStatus::Downloading => 0,
        DownloadStatus::Resolving => 1,
        DownloadStatus::Queued => 2,
        DownloadStatus::Failed => 3,
        DownloadStatus::Completed => 4,
    }
}

/// Dossier de téléchargement par défaut : `~/Téléchargements` ou `~/Downloads`, sinon `.`.
fn default_download_dir() -> String {
    if let Ok(home) = std::env::var("HOME") {
        for name in ["Téléchargements", "Downloads"] {
            let p = PathBuf::from(&home).join(name);
            if p.is_dir() {
                return p.to_string_lossy().into_owned();
            }
        }
        return PathBuf::from(home)
            .join("Downloads")
            .to_string_lossy()
            .into_owned();
    }
    ".".to_string()
}

/// Nettoie un nom de fichier des caractères interdits.
fn sanitize(name: &str) -> String {
    name.chars()
        .map(|c| match c {
            '/' | '\\' | ':' | '*' | '?' | '"' | '<' | '>' | '|' => '_',
            _ => c,
        })
        .collect()
}

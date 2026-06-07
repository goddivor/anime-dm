//! Application egui type IDM : barre de menus + barre d'outils + sidebar catégories +
//! table de téléchargements + barre d'état + dialogues.

use std::path::PathBuf;
use std::sync::mpsc::Receiver;

use eframe::egui;
use egui_extras::{Column, TableBuilder};

use crate::gui::i18n::{t, Lang};
use crate::model::{Anime, DownloadItem, DownloadStatus};
use crate::selection;
use crate::worker::{DownloadJob, Worker, WorkerMsg};

/// Lecteurs proposés dans le menu déroulant, avec leur état réel (vérifié en bout-en-bout).
const PLAYERS: &[(&str, &str)] = &[
    ("LECTEUR myTV", "✓ rapide"),
    ("LECTEUR FHD1", "✓ rapide"),
    ("LECTEUR Stape", "✓ rapide"),
    ("LECTEUR VOE", "✓ navigateur"),
    ("LECTEUR MOON", "navigateur ?"),
    ("LECTEUR SB", "navigateur ?"),
    ("LECTEUR YU", "navigateur ?"),
];

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
    Description,
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

    // --- mode capture d'écran (dev/diagnostic, via la variable d'env ANIME_DM_SHOT) ---
    shot_path: Option<String>,
    shot_frame: u32,
}

impl App {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        cc.egui_ctx.set_theme(egui::ThemePreference::Dark);

        let (tx, rx) = std::sync::mpsc::channel();
        let worker = Worker::new(tx, cc.egui_ctx.clone())
            .expect("initialisation du worker (runtime tokio + client HTTP)");

        let shot_path = std::env::var("ANIME_DM_SHOT").ok();
        let downloads = if shot_path.is_some() {
            demo_items()
        } else {
            Vec::new()
        };

        Self {
            worker,
            rx,
            downloads,
            next_id: 100,
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
            shot_path,
            shot_frame: 0,
        }
    }

    /// En mode capture : demande le screenshot puis l'enregistre en RGBA brut et ferme.
    fn maybe_screenshot(&mut self, ctx: &egui::Context) {
        let Some(path) = self.shot_path.clone() else {
            return;
        };
        self.shot_frame += 1;
        if self.shot_frame == 4 {
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(egui::UserData::default()));
        }
        let captured = ctx.input(|i| {
            i.events.iter().find_map(|e| match e {
                egui::Event::Screenshot { image, .. } => Some(image.clone()),
                _ => None,
            })
        });
        if let Some(image) = captured {
            let [w, h] = image.size;
            let _ = std::fs::write(&path, image.as_raw());
            let _ = std::fs::write(format!("{path}.dim"), format!("{w} {h}"));
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        } else {
            ctx.request_repaint();
        }
    }

    // ------------------------------------------------------------------
    // Actions partagées (appelées par la barre de menus — d'où pub(crate))
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
        let suffix = t(self.lang, "à venir", "coming soon");
        self.status = format!("« {feature} » — {suffix}");
    }

    pub(crate) fn remove_completed(&mut self) {
        let before = self.downloads.len();
        self.downloads
            .retain(|d| d.status != DownloadStatus::Completed);
        let removed = before - self.downloads.len();
        self.set_status(format!(
            "{removed} {}",
            t(self.lang, "terminé(s) retiré(s)", "completed removed")
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
            | SortBy::Description
            | SortBy::Location
            | SortBy::Address
            | SortBy::ParentPage => {}
        }
    }

    // ------------------------------------------------------------------
    // Boucle de messages du worker
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

    fn launch_selection(&mut self) {
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
    // Panneaux
    // ------------------------------------------------------------------

    fn ui_toolbar(&mut self, ui: &mut egui::Ui) {
        let size = if self.toolbar_big { 15.0 } else { 12.0 };
        ui.horizontal(|ui| {
            ui.add_space(4.0);
            if ui
                .add(egui::Button::new(
                    egui::RichText::new(format!(
                        "➕  {}",
                        t(self.lang, "Ajouter une URL", "Add URL")
                    ))
                    .size(size),
                ))
                .clicked()
            {
                self.show_add = true;
            }
            ui.separator();

            let active = self.downloads.iter().filter(|d| is_active(d.status)).count();
            ui.label(format!(
                "{} {} · {active} {}",
                self.downloads.len(),
                t(self.lang, "téléchargement(s)", "download(s)"),
                t(self.lang, "actif(s)", "active")
            ));

            if self.search_open {
                ui.separator();
                ui.label("🔍");
                ui.add(
                    egui::TextEdit::singleline(&mut self.search_query)
                        .hint_text(t(self.lang, "rechercher…", "search…"))
                        .desired_width(160.0),
                );
            }

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label(egui::RichText::new(format!("📁 {}", self.out_dir)).weak());
            });
        });
    }

    fn ui_sidebar(&mut self, ui: &mut egui::Ui) {
        ui.add_space(4.0);
        ui.strong(t(self.lang, "Catégories", "Categories"));
        ui.separator();

        let total = self.downloads.len();
        if ui
            .selectable_label(
                self.category_filter.is_none(),
                format!("📁 {} ({total})", t(self.lang, "Tous", "All")),
            )
            .clicked()
        {
            self.category_filter = None;
        }

        // Catégories dérivées du nom de fichier (« Titre - Ep NNN.mp4 » -> « Titre »).
        let mut cats: Vec<(String, usize)> = Vec::new();
        for d in &self.downloads {
            let cat = category_of(&d.filename);
            match cats.iter_mut().find(|(c, _)| *c == cat) {
                Some((_, n)) => *n += 1,
                None => cats.push((cat, 1)),
            }
        }
        cats.sort_by(|a, b| a.0.cmp(&b.0));
        for (cat, n) in cats {
            let selected = self.category_filter.as_deref() == Some(cat.as_str());
            if ui.selectable_label(selected, format!("🎞 {cat} ({n})")).clicked() {
                self.category_filter = Some(cat);
            }
        }
    }

    fn ui_statusbar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.add_space(6.0);
            ui.label(egui::RichText::new(&self.status).weak());
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if self.speed_limit_enabled {
                    ui.label("🐢");
                }
                ui.label(
                    egui::RichText::new(format!(
                        "{} {}",
                        self.downloads.iter().filter(|d| is_active(d.status)).count(),
                        t(self.lang, "en cours", "running")
                    ))
                    .weak(),
                );
            });
        });
    }

    fn ui_table(&mut self, ui: &mut egui::Ui) {
        // Filtre : recherche + catégorie sélectionnée.
        let query = self.search_query.to_lowercase();
        let visible: Vec<u64> = self
            .downloads
            .iter()
            .filter(|d| query.is_empty() || d.filename.to_lowercase().contains(&query))
            .filter(|d| {
                self.category_filter
                    .as_deref()
                    .map_or(true, |c| category_of(&d.filename) == c)
            })
            .map(|d| d.id)
            .collect();

        if self.downloads.is_empty() {
            ui.vertical_centered(|ui| {
                ui.add_space(60.0);
                ui.label(
                    egui::RichText::new(t(self.lang, "Aucun téléchargement", "No downloads"))
                        .size(18.0)
                        .weak(),
                );
                ui.add_space(6.0);
                ui.label(
                    egui::RichText::new(t(
                        self.lang,
                        "Menu Tâches › Ajouter, ou colle un lien d'animé voir-anime.to",
                        "Tasks menu › Add, or paste a voir-anime.to link",
                    ))
                    .weak(),
                );
            });
            return;
        }

        let lang = self.lang;
        let selected = self.selected;
        let mut clicked: Option<u64> = None;

        TableBuilder::new(ui)
            .striped(true)
            .resizable(true)
            .cell_layout(egui::Layout::left_to_right(egui::Align::Center))
            .column(Column::auto().at_least(28.0))
            .column(Column::remainder().at_least(180.0))
            .column(Column::auto().at_least(120.0))
            .column(Column::initial(200.0).at_least(120.0))
            .column(Column::auto().at_least(60.0))
            .header(22.0, |mut header| {
                for title in [
                    "",
                    t(lang, "Fichier", "File"),
                    t(lang, "État", "Status"),
                    t(lang, "Progression", "Progress"),
                    t(lang, "Vitesse", "Speed"),
                ] {
                    header.col(|ui| {
                        ui.strong(title);
                    });
                }
            })
            .body(|mut body| {
                for d in self.downloads.iter().filter(|d| visible.contains(&d.id)) {
                    body.row(26.0, |mut row| {
                        row.col(|ui| {
                            ui.label(d.status.icon());
                        });
                        row.col(|ui| {
                            let resp = ui.selectable_label(selected == Some(d.id), &d.filename);
                            if resp.clicked() {
                                clicked = Some(d.id);
                            }
                            if let Some(err) = &d.error {
                                resp.on_hover_text(err);
                            }
                        });
                        row.col(|ui| {
                            ui.label(status_label(lang, d.status));
                        });
                        row.col(|ui| {
                            if d.status == DownloadStatus::Downloading && d.progress < 0.0 {
                                ui.spinner();
                                ui.label(t(lang, "démarrage…", "starting…"));
                            } else {
                                ui.add(
                                    egui::ProgressBar::new(d.progress.clamp(0.0, 1.0))
                                        .show_percentage()
                                        .desired_width(180.0),
                                );
                            }
                        });
                        row.col(|ui| {
                            ui.label(&d.speed);
                        });
                    });
                }
            });

        if let Some(id) = clicked {
            self.selected = Some(id);
        }
    }

    fn ui_add_dialog(&mut self, ctx: &egui::Context) {
        if !self.show_add {
            return;
        }
        let mut open = true;
        let mut do_validate = false;
        let mut do_launch = false;
        let lang = self.lang;

        egui::Window::new(t(lang, "Ajouter un téléchargement", "Add a download"))
            .open(&mut open)
            .collapsible(false)
            .resizable(true)
            .default_width(460.0)
            .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
            .show(ctx, |ui| {
                ui.label(t(lang, "Lien de l'animé :", "Anime link:"));
                ui.horizontal(|ui| {
                    let resp = ui.add(
                        egui::TextEdit::singleline(&mut self.url_input)
                            .hint_text("https://voir-anime.to/anime/dragon-ball-vf/")
                            .desired_width(320.0),
                    );
                    let enter =
                        resp.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                    if ui.button(t(lang, "Valider", "Confirm")).clicked() || enter {
                        do_validate = true;
                    }
                });

                if self.loading_anime {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(t(lang, "Récupération des épisodes…", "Fetching episodes…"));
                    });
                }
                if let Some(err) = &self.load_error {
                    ui.colored_label(egui::Color32::LIGHT_RED, err);
                }

                if let Some(anime) = self.anime.clone() {
                    ui.separator();
                    ui.heading(&anime.title);
                    ui.label(format!(
                        "{} {}",
                        anime.episodes.len(),
                        t(lang, "épisode(s)", "episode(s)")
                    ));
                    ui.add_space(6.0);

                    egui::Grid::new("dl_options")
                        .num_columns(2)
                        .spacing([12.0, 8.0])
                        .show(ui, |ui| {
                            ui.label(t(lang, "Épisodes :", "Episodes:"));
                            ui.add(
                                egui::TextEdit::singleline(&mut self.selection_input)
                                    .hint_text("ex : 1-20  ou  1,5,8")
                                    .desired_width(260.0),
                            );
                            ui.end_row();

                            ui.label(t(lang, "Lecteur :", "Player:"));
                            egui::ComboBox::from_id_salt("player_combo")
                                .selected_text(self.selected_player.clone())
                                .show_ui(ui, |ui| {
                                    for (p, st) in PLAYERS {
                                        ui.selectable_value(
                                            &mut self.selected_player,
                                            p.to_string(),
                                            format!("{p}  {st}"),
                                        );
                                    }
                                });
                            ui.end_row();

                            ui.label(t(lang, "Dossier :", "Folder:"));
                            ui.add(
                                egui::TextEdit::singleline(&mut self.out_dir).desired_width(260.0),
                            );
                            ui.end_row();
                        });

                    ui.add_space(8.0);
                    let preview = selection::parse(&self.selection_input, anime.episodes.len());
                    if ui
                        .add_enabled(
                            !preview.is_empty(),
                            egui::Button::new(
                                egui::RichText::new(format!(
                                    "⬇  {} ({})",
                                    t(lang, "Télécharger", "Download"),
                                    preview.len()
                                ))
                                .size(15.0),
                            ),
                        )
                        .clicked()
                    {
                        do_launch = true;
                    }
                }
            });

        if do_validate {
            let url = self.url_input.trim().to_string();
            if !url.is_empty() {
                self.loading_anime = true;
                self.load_error = None;
                self.anime = None;
                self.worker.load_anime(url);
            }
        }
        if do_launch {
            self.launch_selection();
        }
        if !open {
            self.show_add = false;
        }
    }

    fn ui_simple_dialogs(&mut self, ctx: &egui::Context) {
        let lang = self.lang;

        if self.show_about {
            let mut open = true;
            egui::Window::new(t(lang, "À propos", "About"))
                .open(&mut open)
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.heading("Anime Download Manager");
                    ui.label("v0.1.0");
                    ui.add_space(4.0);
                    ui.label(t(
                        lang,
                        "Gestionnaire de téléchargement type IDM pour sites d'animés.",
                        "IDM-like download manager for anime sites.",
                    ));
                    ui.separator();
                    ui.label(t(lang, "Auteurs : —", "Authors: —"));
                    ui.label(t(lang, "Licence : —", "License: —"));
                });
            self.show_about = open;
        }

        if self.show_help {
            let mut open = true;
            egui::Window::new(t(lang, "Aide", "Help"))
                .open(&mut open)
                .collapsible(false)
                .resizable(true)
                .default_width(420.0)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(t(
                        lang,
                        "1. Menu Tâches › Ajouter, colle un lien d'animé.",
                        "1. Tasks menu › Add, paste an anime link.",
                    ));
                    ui.label(t(
                        lang,
                        "2. Choisis les épisodes (1-20 / 1,5,8) et un lecteur.",
                        "2. Pick episodes (1-20 / 1,5,8) and a player.",
                    ));
                    ui.label(t(
                        lang,
                        "3. Le téléchargement démarre en parallèle.",
                        "3. Downloads start in parallel.",
                    ));
                    ui.separator();
                    ui.hyperlink_to(
                        t(lang, "Wiki en ligne", "Online wiki"),
                        "https://example.com/wiki",
                    );
                    ui.hyperlink_to("FAQ", "https://example.com/faq");
                });
            self.show_help = open;
        }

        if self.show_manual {
            let mut open = true;
            let mut do_add = false;
            egui::Window::new(t(lang, "Téléchargement manuel", "Manual download"))
                .open(&mut open)
                .collapsible(false)
                .resizable(true)
                .default_width(460.0)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(t(
                        lang,
                        "Colle une ou plusieurs URL d'épisodes (une par ligne) :",
                        "Paste one or more episode URLs (one per line):",
                    ));
                    ui.add(
                        egui::TextEdit::multiline(&mut self.manual_input)
                            .desired_rows(6)
                            .desired_width(430.0)
                            .hint_text("https://voir-anime.to/anime/.../episode-1-vf/"),
                    );
                    if ui.button(t(lang, "Ajouter", "Add")).clicked() {
                        do_add = true;
                    }
                });
            if do_add {
                self.soon(t(lang, "Téléchargement manuel", "Manual download"));
                self.show_manual = false;
            } else {
                self.show_manual = open;
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
            self.soon(t(self.lang, "Lot depuis le presse-papier", "Batch from clipboard"));
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
            .exact_size(28.0)
            .show_inside(ui, |ui| self.show_menu_bar(ui));
        egui::Panel::top("toolbar")
            .exact_size(40.0)
            .show_inside(ui, |ui| self.ui_toolbar(ui));
        egui::Panel::bottom("statusbar")
            .exact_size(24.0)
            .show_inside(ui, |ui| self.ui_statusbar(ui));
        if self.show_categories {
            egui::Panel::left("categories")
                .exact_size(190.0)
                .show_inside(ui, |ui| self.ui_sidebar(ui));
        }
        egui::CentralPanel::default().show_inside(ui, |ui| self.ui_table(ui));

        self.ui_add_dialog(&ctx);
        self.ui_simple_dialogs(&ctx);
        self.maybe_screenshot(&ctx);

        let busy =
            self.loading_anime || self.downloads.iter().any(|d| is_active(d.status));
        if busy {
            ctx.request_repaint_after(std::time::Duration::from_millis(250));
        }
    }
}

/// Quelques téléchargements factices pour peupler l'UI en mode capture/démo.
fn demo_items() -> Vec<DownloadItem> {
    let mk = |id, filename: &str, status, progress, speed: &str| DownloadItem {
        id,
        filename: filename.to_string(),
        status,
        progress,
        speed: speed.to_string(),
        total_secs: 1420.0,
        error: None,
    };
    vec![
        mk(1, "Dragon Ball (VF) - Ep 001.mp4", DownloadStatus::Completed, 1.0, ""),
        mk(2, "Dragon Ball (VF) - Ep 002.mp4", DownloadStatus::Downloading, 0.47, "2.4x"),
        mk(3, "Dragon Ball (VF) - Ep 003.mp4", DownloadStatus::Queued, -1.0, ""),
        mk(4, "Naruto - Ep 015.mp4", DownloadStatus::Downloading, 0.12, "1.1x"),
        mk(5, "One Piece - Ep 1080.mp4", DownloadStatus::Failed, 0.0, ""),
    ]
}

// ----------------------------------------------------------------------
// Helpers libres
// ----------------------------------------------------------------------

fn is_active(s: DownloadStatus) -> bool {
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

fn status_label(lang: Lang, s: DownloadStatus) -> &'static str {
    match s {
        DownloadStatus::Queued => t(lang, "En attente", "Queued"),
        DownloadStatus::Resolving => t(lang, "Résolution", "Resolving"),
        DownloadStatus::Downloading => t(lang, "Téléchargement", "Downloading"),
        DownloadStatus::Completed => t(lang, "Terminé", "Completed"),
        DownloadStatus::Failed => t(lang, "Échec", "Failed"),
    }
}

/// Catégorie d'un fichier = partie avant « - Ep » (le titre de l'animé).
fn category_of(filename: &str) -> String {
    filename
        .split(" - Ep")
        .next()
        .unwrap_or(filename)
        .trim()
        .to_string()
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

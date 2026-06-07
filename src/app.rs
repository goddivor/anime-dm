//! Interface egui type IDM : barre d'outils + table de téléchargements + dialogue d'ajout.

use std::path::PathBuf;
use std::sync::mpsc::Receiver;

use eframe::egui;
use egui_extras::{Column, TableBuilder};

use crate::model::{Anime, DownloadItem, DownloadStatus};
use crate::selection;
use crate::worker::{DownloadJob, Worker, WorkerMsg};

/// Lecteurs proposés dans le menu déroulant, avec leur état réel (vérifié en bout-en-bout).
/// Le suffixe informe l'utilisateur sur la fiabilité du lecteur choisi.
const PLAYERS: &[(&str, &str)] = &[
    ("LECTEUR myTV", "✓ rapide"),       // vidmoly    -> HLS (HTTP)
    ("LECTEUR FHD1", "✓ rapide"),       // my.mail.ru -> MP4 1080p (HTTP, endpoint meta)
    ("LECTEUR Stape", "✓ rapide"),      // streamtape -> MP4 (HTTP)
    ("LECTEUR VOE", "✓ navigateur"),    // voe.sx     -> headless (OK)
    ("LECTEUR MOON", "navigateur ?"),   // SPA « Byse »
    ("LECTEUR SB", "navigateur ?"),     // streamhide / Byse
    ("LECTEUR YU", "navigateur ?"),     // yourupload (souvent DMCA)
];

pub struct App {
    worker: Worker,
    rx: Receiver<WorkerMsg>,
    downloads: Vec<DownloadItem>,
    next_id: u64,
    out_dir: String,

    // --- état du dialogue d'ajout ---
    show_add: bool,
    url_input: String,
    loading_anime: bool,
    anime: Option<Anime>,
    load_error: Option<String>,
    selection_input: String,
    selected_player: String,
}

impl App {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        // Thème sombre par défaut, dans l'esprit de la capture fournie.
        cc.egui_ctx.set_visuals(egui::Visuals::dark());

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
        }
    }

    /// Vide le canal des messages du worker et met à jour l'état de l'UI.
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

        // On referme le dialogue et on réinitialise pour un prochain ajout.
        self.show_add = false;
        self.anime = None;
        self.url_input.clear();
        self.selection_input.clear();
        self.load_error = None;
    }

    fn ui_toolbar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.add_space(4.0);
            if ui
                .add(egui::Button::new(egui::RichText::new("➕  Ajouter une URL").size(15.0)))
                .clicked()
            {
                self.show_add = true;
            }
            ui.separator();

            let active = self
                .downloads
                .iter()
                .filter(|d| {
                    matches!(
                        d.status,
                        DownloadStatus::Downloading
                            | DownloadStatus::Resolving
                            | DownloadStatus::Queued
                    )
                })
                .count();
            ui.label(format!(
                "{} téléchargement(s) · {active} actif(s)",
                self.downloads.len()
            ));

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label(egui::RichText::new(format!("📁 {}", self.out_dir)).weak());
            });
        });
    }

    fn ui_table(&mut self, ui: &mut egui::Ui) {
        if self.downloads.is_empty() {
            ui.vertical_centered(|ui| {
                ui.add_space(60.0);
                ui.label(
                    egui::RichText::new("Aucun téléchargement")
                        .size(18.0)
                        .weak(),
                );
                ui.add_space(6.0);
                ui.label(
                    egui::RichText::new(
                        "Clique sur « Ajouter une URL » et colle un lien d'animé voir-anime.to",
                    )
                    .weak(),
                );
            });
            return;
        }

        TableBuilder::new(ui)
            .striped(true)
            .resizable(true)
            .cell_layout(egui::Layout::left_to_right(egui::Align::Center))
            .column(Column::auto().at_least(28.0)) // icône
            .column(Column::remainder().at_least(180.0)) // fichier
            .column(Column::auto().at_least(70.0)) // état
            .column(Column::initial(220.0).at_least(120.0)) // progression
            .column(Column::auto().at_least(60.0)) // vitesse
            .header(22.0, |mut header| {
                for title in ["", "Fichier", "État", "Progression", "Vitesse"] {
                    header.col(|ui| {
                        ui.strong(title);
                    });
                }
            })
            .body(|mut body| {
                for d in &self.downloads {
                    body.row(26.0, |mut row| {
                        row.col(|ui| {
                            ui.label(d.status.icon());
                        });
                        row.col(|ui| {
                            let resp = ui.label(&d.filename);
                            if let Some(err) = &d.error {
                                resp.on_hover_text(err);
                            }
                        });
                        row.col(|ui| {
                            ui.label(d.status.label());
                        });
                        row.col(|ui| {
                            if d.status == DownloadStatus::Downloading && d.progress < 0.0 {
                                ui.spinner();
                                ui.label("démarrage…");
                            } else {
                                let p = d.progress.clamp(0.0, 1.0);
                                ui.add(
                                    egui::ProgressBar::new(p)
                                        .show_percentage()
                                        .desired_width(200.0),
                                );
                            }
                        });
                        row.col(|ui| {
                            ui.label(&d.speed);
                        });
                    });
                }
            });
    }

    fn ui_add_dialog(&mut self, ctx: &egui::Context) {
        if !self.show_add {
            return;
        }
        let mut open = true;
        let mut do_validate = false;
        let mut do_launch = false;

        egui::Window::new("Ajouter un téléchargement")
            .open(&mut open)
            .collapsible(false)
            .resizable(true)
            .default_width(460.0)
            .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
            .show(ctx, |ui| {
                ui.label("Lien de l'animé :");
                ui.horizontal(|ui| {
                    let resp = ui.add(
                        egui::TextEdit::singleline(&mut self.url_input)
                            .hint_text("https://voir-anime.to/anime/dragon-ball-vf/")
                            .desired_width(320.0),
                    );
                    let enter =
                        resp.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                    if ui.button("Valider").clicked() || enter {
                        do_validate = true;
                    }
                });

                if self.loading_anime {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label("Récupération des épisodes…");
                    });
                }
                if let Some(err) = &self.load_error {
                    ui.colored_label(egui::Color32::LIGHT_RED, err);
                }

                if let Some(anime) = self.anime.clone() {
                    ui.separator();
                    ui.heading(&anime.title);
                    ui.label(format!("{} épisode(s) détecté(s)", anime.episodes.len()));
                    ui.add_space(6.0);

                    egui::Grid::new("dl_options")
                        .num_columns(2)
                        .spacing([12.0, 8.0])
                        .show(ui, |ui| {
                            ui.label("Épisodes :");
                            ui.add(
                                egui::TextEdit::singleline(&mut self.selection_input)
                                    .hint_text("ex : 1-20  ou  1,5,8  (vide = tous)")
                                    .desired_width(260.0),
                            );
                            ui.end_row();

                            ui.label("Lecteur :");
                            egui::ComboBox::from_id_salt("player_combo")
                                .selected_text(self.selected_player.clone())
                                .show_ui(ui, |ui| {
                                    for (p, status) in PLAYERS {
                                        ui.selectable_value(
                                            &mut self.selected_player,
                                            p.to_string(),
                                            format!("{p}  {status}"),
                                        );
                                    }
                                });
                            ui.end_row();

                            ui.label("Dossier :");
                            ui.add(
                                egui::TextEdit::singleline(&mut self.out_dir)
                                    .desired_width(260.0),
                            );
                            ui.end_row();
                        });

                    ui.add_space(8.0);
                    let preview = selection::parse(&self.selection_input, anime.episodes.len());
                    ui.horizontal(|ui| {
                        if ui
                            .add_enabled(
                                !preview.is_empty(),
                                egui::Button::new(
                                    egui::RichText::new(format!(
                                        "⬇  Télécharger ({})",
                                        preview.len()
                                    ))
                                    .size(15.0),
                                ),
                            )
                            .clicked()
                        {
                            do_launch = true;
                        }
                        ui.label(
                            egui::RichText::new("le téléchargement démarre en parallèle").weak(),
                        );
                    });
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
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.drain_messages();
        let ctx = ui.ctx().clone();

        egui::Panel::top("toolbar")
            .exact_size(40.0)
            .show_inside(ui, |ui| self.ui_toolbar(ui));

        egui::CentralPanel::default().show_inside(ui, |ui| self.ui_table(ui));

        self.ui_add_dialog(&ctx);

        // Repaint régulier tant que des tâches tournent (progression fluide).
        let busy = self.loading_anime
            || self.downloads.iter().any(|d| {
                matches!(
                    d.status,
                    DownloadStatus::Downloading
                        | DownloadStatus::Resolving
                        | DownloadStatus::Queued
                )
            });
        if busy {
            ctx.request_repaint_after(std::time::Duration::from_millis(250));
        }
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

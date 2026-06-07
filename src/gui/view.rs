//! Panneaux de la vue principale : barre d'outils, sidebar des catégories, table des
//! téléchargements et barre d'état. (Dialogues : voir `dialogs.rs` ; menus : `menu.rs`.)

use eframe::egui;
use egui_extras::{Column, TableBuilder};

use crate::gui::app::{is_active, App};
use crate::gui::i18n::{t, Lang};
use crate::model::DownloadStatus;

impl App {
    pub(crate) fn ui_toolbar(&mut self, ui: &mut egui::Ui) {
        let size = if self.toolbar_big { 15.0 } else { 12.0 };
        ui.horizontal(|ui| {
            ui.add_space(4.0);
            if ui
                .add(egui::Button::new(
                    egui::RichText::new(format!(
                        "➕  {}",
                        t(self.lang, "add_url")
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
                t(self.lang, "download_s"),
                t(self.lang, "active")
            ));

            if self.search_open {
                ui.separator();
                ui.label("🔍");
                ui.add(
                    egui::TextEdit::singleline(&mut self.search_query)
                        .hint_text(t(self.lang, "search"))
                        .desired_width(160.0),
                );
            }

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label(egui::RichText::new(format!("📁 {}", self.out_dir)).weak());
            });
        });
    }

    pub(crate) fn ui_sidebar(&mut self, ui: &mut egui::Ui) {
        ui.add_space(4.0);
        ui.strong(t(self.lang, "categories"));
        ui.separator();

        let total = self.downloads.len();
        if ui
            .selectable_label(
                self.category_filter.is_none(),
                format!("📁 {} ({total})", t(self.lang, "all")),
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

    pub(crate) fn ui_statusbar(&mut self, ui: &mut egui::Ui) {
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
                        t(self.lang, "running")
                    ))
                    .weak(),
                );
            });
        });
    }

    pub(crate) fn ui_table(&mut self, ui: &mut egui::Ui) {
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
                    egui::RichText::new(t(self.lang, "no_downloads"))
                        .size(18.0)
                        .weak(),
                );
                ui.add_space(6.0);
                ui.label(
                    egui::RichText::new(t(self.lang, "tasks_menu_add_or_paste_a_voir_anime_to_"))
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
            .column(Column::exact(24.0)) // icône
            .column(Column::initial(210.0).at_least(130.0).clip(true)) // fichier
            .column(Column::exact(110.0)) // état
            .column(Column::exact(150.0)) // progression
            .column(Column::remainder().at_least(60.0)) // vitesse (dernière -> absorbe le reste)
            .header(22.0, |mut header| {
                for title in [
                    "",
                    t(lang, "file"),
                    t(lang, "status"),
                    t(lang, "progress"),
                    t(lang, "speed"),
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
                                ui.label(t(lang, "starting"));
                            } else {
                                ui.add(
                                    egui::ProgressBar::new(d.progress.clamp(0.0, 1.0))
                                        .show_percentage()
                                        .desired_width(140.0),
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
}

fn status_label(lang: Lang, s: DownloadStatus) -> &'static str {
    match s {
        DownloadStatus::Queued => t(lang, "queued"),
        DownloadStatus::Resolving => t(lang, "resolving"),
        DownloadStatus::Downloading => t(lang, "downloading"),
        DownloadStatus::Completed => t(lang, "completed"),
        DownloadStatus::Failed => t(lang, "failed"),
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

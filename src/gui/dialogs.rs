//! Fenêtres modales : ajout de téléchargement, À propos, Aide, Téléchargement manuel.

use eframe::egui;

use crate::gui::app::App;
use crate::gui::i18n::t;
use crate::selection;

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

impl App {
    pub(crate) fn ui_add_dialog(&mut self, ctx: &egui::Context) {
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

    pub(crate) fn ui_simple_dialogs(&mut self, ctx: &egui::Context) {
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
}

use eframe::egui;

use crate::gui::app::App;
use crate::gui::i18n::t;
use crate::selection;

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

        egui::Window::new(t(lang, "dialog.add.title"))
            .open(&mut open)
            .collapsible(false)
            .resizable(true)
            .default_width(460.0)
            .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
            .show(ctx, |ui| {
                ui.label(t(lang, "dialog.add.link_label"));
                ui.horizontal(|ui| {
                    let resp = ui.add(
                        egui::TextEdit::singleline(&mut self.url_input)
                            .hint_text("https://voir-anime.to/anime/dragon-ball-vf/")
                            .desired_width(320.0),
                    );
                    let enter = resp.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                    if ui.button(t(lang, "dialog.add.confirm")).clicked() || enter {
                        do_validate = true;
                    }
                });

                if self.loading_anime {
                    ui.horizontal(|ui| {
                        ui.spinner();
                        ui.label(t(lang, "dialog.add.fetching"));
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
                        t(lang, "dialog.add.episodes_count")
                    ));
                    ui.add_space(6.0);

                    egui::Grid::new("dl_options")
                        .num_columns(2)
                        .spacing([12.0, 8.0])
                        .show(ui, |ui| {
                            ui.label(t(lang, "dialog.add.episodes_label"));
                            ui.add(
                                egui::TextEdit::singleline(&mut self.selection_input)
                                    .hint_text("ex : 1-20  ou  1,5,8")
                                    .desired_width(260.0),
                            );
                            ui.end_row();

                            ui.label(t(lang, "dialog.add.player_label"));
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

                            ui.label(t(lang, "dialog.add.folder_label"));
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
                                    t(lang, "dialog.add.download_btn"),
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
            egui::Window::new(t(lang, "dialog.about.title"))
                .open(&mut open)
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.heading("Anime Download Manager");
                    ui.label("v0.1.0");
                    ui.add_space(4.0);
                    ui.label(t(lang, "dialog.about.description"));
                    ui.separator();
                    ui.label(t(lang, "dialog.about.authors"));
                    ui.label(t(lang, "dialog.about.license"));
                });
            self.show_about = open;
        }

        if self.show_help {
            let mut open = true;
            egui::Window::new(t(lang, "dialog.help.title"))
                .open(&mut open)
                .collapsible(false)
                .resizable(true)
                .default_width(420.0)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(t(lang, "dialog.help.step1"));
                    ui.label(t(lang, "dialog.help.step2"));
                    ui.label(t(lang, "dialog.help.step3"));
                    ui.separator();
                    ui.hyperlink_to(t(lang, "dialog.help.wiki"), "https://example.com/wiki");
                    ui.hyperlink_to("FAQ", "https://example.com/faq");
                });
            self.show_help = open;
        }

        if self.show_manual {
            let mut open = true;
            let mut do_add = false;
            egui::Window::new(t(lang, "dialog.manual.title"))
                .open(&mut open)
                .collapsible(false)
                .resizable(true)
                .default_width(460.0)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(t(lang, "dialog.manual.hint"));
                    ui.add(
                        egui::TextEdit::multiline(&mut self.manual_input)
                            .desired_rows(6)
                            .desired_width(430.0)
                            .hint_text("https://voir-anime.to/anime/.../episode-1-vf/"),
                    );
                    if ui.button(t(lang, "dialog.manual.add")).clicked() {
                        do_add = true;
                    }
                });
            if do_add {
                self.soon(t(lang, "dialog.manual.title"));
                self.show_manual = false;
            } else {
                self.show_manual = open;
            }
        }
    }
}

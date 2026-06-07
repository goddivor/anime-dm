//! Barre de menus type IDM : Tâches / Fichier / Téléchargement / Affichage / Aide.
//!
//! La plupart des items pilotent l'état UI (thème, langue, tri, recherche…) ; ceux qui
//! dépendent d'un backend encore absent (files d'attente, planificateur, export/import,
//! limiteur/booster) sont des coquilles qui affichent « à venir » dans la barre d'état.

use eframe::egui;

use crate::gui::app::{App, SortBy, ThemeMode};
use crate::gui::i18n::{t, Lang};

impl App {
    pub(crate) fn show_menu_bar(&mut self, ui: &mut egui::Ui) {
        let lang = self.lang;
        let ctx = ui.ctx().clone();

        egui::MenuBar::new().ui(ui, |ui| {
            // ===================== Tâches =====================
            ui.menu_button(t(lang, "tasks"), |ui| {
                if ui
                    .button(t(lang, "add_new_download"))
                    .clicked()
                {
                    self.show_add = true;
                }
                if ui
                    .button(t(lang, "manual_download"))
                    .clicked()
                {
                    self.show_manual = true;
                }
                if ui
                    .add(
                        egui::Button::new(t(lang, "batch_download_clipboard"))
                        .shortcut_text("Ctrl+Maj+V"),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "batch_from_clipboard"));
                }

                ui.separator();
                ui.menu_button(t(lang, "export"), |ui| {
                    self.export_import_items(ui, lang, t(lang, "export_2"));
                });
                ui.menu_button(t(lang, "import"), |ui| {
                    self.export_import_items(ui, lang, t(lang, "import_2"));
                });

                ui.separator();
                if ui.button(t(lang, "quit")).clicked() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });

            // ===================== Fichier =====================
            // Items contextuels : actifs seulement si une ligne est sélectionnée.
            ui.menu_button(t(lang, "file"), |ui| {
                let has = self.selected.is_some();
                if ui
                    .add_enabled(
                        has,
                        egui::Button::new(t(lang, "start_download")),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "start_download"));
                }
                if ui
                    .add_enabled(
                        has,
                        egui::Button::new(t(lang, "stop_download")),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "stop_download"));
                }
                if ui
                    .add_enabled(has, egui::Button::new(t(lang, "re_download")))
                    .clicked()
                {
                    self.soon(t(lang, "re_download"));
                }
                ui.separator();
                if ui
                    .add_enabled(has, egui::Button::new(t(lang, "remove")))
                    .clicked()
                {
                    self.remove_selected();
                }
            });

            // ===================== Téléchargement =====================
            ui.menu_button(t(lang, "download_2"), |ui| {
                if ui.button(t(lang, "pause_all")).clicked() {
                    self.soon(t(lang, "pause_all"));
                }
                if ui.button(t(lang, "stop_all")).clicked() {
                    self.soon(t(lang, "stop_all"));
                }
                if ui
                    .button(t(lang, "remove_completed"))
                    .clicked()
                {
                    self.remove_completed();
                }
                if ui
                    .add(egui::Button::new(t(lang, "search_2")).shortcut_text("Ctrl+F"))
                    .clicked()
                {
                    self.search_open = !self.search_open;
                }

                ui.separator();
                if ui.button(t(lang, "schedule")).clicked() {
                    self.soon(t(lang, "schedule"));
                }
                ui.menu_button(t(lang, "start_queue"), |ui| {
                    if ui.button(t(lang, "main_queue")).clicked() {
                        self.soon(t(lang, "main_queue"));
                    }
                    if ui.button(t(lang, "sync_queue")).clicked() {
                        self.soon(t(lang, "sync_queue"));
                    }
                });
                ui.menu_button(t(lang, "stop_queue"), |ui| {
                    if ui.button(t(lang, "main_queue")).clicked() {
                        self.soon(t(lang, "main_queue"));
                    }
                    if ui.button(t(lang, "sync_queue")).clicked() {
                        self.soon(t(lang, "sync_queue"));
                    }
                });
                ui.menu_button(t(lang, "speed_limiter"), |ui| {
                    if ui.radio(self.speed_limit_enabled, t(lang, "enable")).clicked() {
                        self.speed_limit_enabled = true;
                        self.set_status(t(lang, "limiter_enabled"));
                    }
                    if ui
                        .radio(!self.speed_limit_enabled, t(lang, "disable"))
                        .clicked()
                    {
                        self.speed_limit_enabled = false;
                        self.set_status(t(lang, "limiter_disabled"));
                    }
                    if ui.button(t(lang, "settings")).clicked() {
                        self.soon(t(lang, "limiter_settings"));
                    }
                });
                if ui.button(t(lang, "speed_booster")).clicked() {
                    self.soon(t(lang, "speed_booster"));
                }
            });

            // ===================== Affichage =====================
            ui.menu_button(t(lang, "view"), |ui| {
                ui.checkbox(
                    &mut self.show_categories,
                    t(lang, "categories_panel"),
                );

                ui.menu_button(t(lang, "sort_files"), |ui| {
                    let opts: &[(SortBy, &str)] = &[
                        (SortBy::DateAdded, "sort.date_added"),
                        (SortBy::Name, "sort.name"),
                        (SortBy::Size, "sort.size"),
                        (SortBy::Status, "sort.status"),
                        (SortBy::TimeLeft, "sort.time_left"),
                        (SortBy::Speed, "sort.speed"),
                        (SortBy::LastTry, "sort.last_try"),
                        (SortBy::Description, "sort.description"),
                        (SortBy::Location, "sort.location"),
                        (SortBy::Address, "sort.address"),
                        (SortBy::ParentPage, "sort.parent_page"),
                    ];
                    for (variant, key) in opts {
                        if ui.radio_value(&mut self.sort_by, *variant, t(lang, *key)).clicked() {
                            self.sort_downloads();
                        }
                    }
                });

                ui.menu_button(t(lang, "toolbar"), |ui| {
                    if ui.button(t(lang, "customize")).clicked() {
                        self.soon(t(lang, "customize_toolbar"));
                    }
                    ui.radio_value(&mut self.toolbar_big, true, t(lang, "big_buttons"));
                    ui.radio_value(
                        &mut self.toolbar_big,
                        false,
                        t(lang, "small_buttons"),
                    );
                    if ui.button(t(lang, "interface")).clicked() {
                        self.soon(t(lang, "interface_2"));
                    }
                    if ui.button(t(lang, "shortcuts")).clicked() {
                        self.soon(t(lang, "shortcuts_2"));
                    }
                });

                ui.checkbox(
                    &mut self.notifications_enabled,
                    t(lang, "notifications"),
                );
                if ui
                    .button(t(lang, "customize_columns"))
                    .clicked()
                {
                    self.soon(t(lang, "displayed_columns"));
                }

                ui.menu_button(t(lang, "mode"), |ui| {
                    let modes: &[(ThemeMode, &str)] = &[
                        (ThemeMode::Dark, "mode.dark"),
                        (ThemeMode::Light, "mode.light"),
                        (ThemeMode::System, "mode.system"),
                    ];
                    for (variant, key) in modes {
                        if ui.radio_value(&mut self.theme, *variant, t(lang, *key)).clicked() {
                            self.apply_theme(&ctx);
                        }
                    }
                });

                ui.menu_button(t(lang, "font"), |ui| {
                    if ui.button(t(lang, "select_font")).clicked() {
                        self.soon(t(lang, "font_selection"));
                    }
                    if ui
                        .button(t(lang, "reset_default_font"))
                        .clicked()
                    {
                        self.soon(t(lang, "default_font"));
                    }
                });

                ui.menu_button(t(lang, "language"), |ui| {
                    ui.radio_value(&mut self.lang, Lang::En, Lang::En.native_name());
                    ui.radio_value(&mut self.lang, Lang::Fr, Lang::Fr.native_name());
                });
            });

            // ===================== Aide =====================
            ui.menu_button(t(lang, "help"), |ui| {
                if ui.add(egui::Button::new(t(lang, "help")).shortcut_text("F1")).clicked() {
                    self.show_help = true;
                }
                if ui.button(t(lang, "quick_update")).clicked() {
                    self.soon(t(lang, "quick_update"));
                }
                ui.menu_button(t(lang, "about"), |ui| {
                    if ui.button(t(lang, "about")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "authors_2")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "license_2")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "credits")).clicked() {
                        self.show_about = true;
                    }
                });
            });
        });
    }

    /// Items partagés des sous-menus Exporter / Importer (4 formats).
    fn export_import_items(&mut self, ui: &mut egui::Ui, lang: Lang, verb: &str) {
        let formats = [
            t(lang, "adm_file_our_format"),
            t(lang, "text_file_txt"),
            t(lang, "json_file_json"),
            t(lang, "excel_file_xlsx"),
        ];
        for f in formats {
            if ui.button(f).clicked() {
                self.soon(&format!("{verb} {f}"));
            }
        }
    }
}

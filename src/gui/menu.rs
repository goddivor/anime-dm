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
            // Écart plus large entre les menus du haut (sans toucher au reste de l'UI).
            ui.spacing_mut().item_spacing.x = 20.0;

            // ===================== Tâches =====================
            let it_tasks = ui.menu_button(t(lang, "menu.tasks.title"), |ui| {
                if ui
                    .button(t(lang, "menu.tasks.add"))
                    .clicked()
                {
                    self.show_add = true;
                }
                if ui
                    .button(t(lang, "menu.tasks.manual"))
                    .clicked()
                {
                    self.show_manual = true;
                }
                if ui
                    .add(
                        egui::Button::new(t(lang, "menu.tasks.batch"))
                        .shortcut_text("Ctrl+Maj+V"),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "status.batch_clipboard"));
                }

                ui.separator();
                ui.menu_button(t(lang, "menu.tasks.export"), |ui| {
                    self.export_import_items(
                        ui,
                        lang,
                        t(lang, "menu.tasks.export_verb"),
                        &[
                            "menu.tasks.export.fmt_adm",
                            "menu.tasks.export.fmt_txt",
                            "menu.tasks.export.fmt_json",
                            "menu.tasks.export.fmt_sheet",
                        ],
                    );
                });
                ui.menu_button(t(lang, "menu.tasks.import"), |ui| {
                    self.export_import_items(
                        ui,
                        lang,
                        t(lang, "menu.tasks.import_verb"),
                        &[
                            "menu.tasks.import.fmt_adm",
                            "menu.tasks.import.fmt_txt",
                            "menu.tasks.import.fmt_json",
                            "menu.tasks.import.fmt_sheet",
                        ],
                    );
                });

                ui.separator();
                if ui.button(t(lang, "menu.tasks.quit")).clicked() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });

            // ===================== Fichier =====================
            // Items contextuels : actifs seulement si une ligne est sélectionnée.
            let it_file = ui.menu_button(t(lang, "menu.file.title"), |ui| {
                let has = self.selected.is_some();
                if ui
                    .add_enabled(
                        has,
                        egui::Button::new(t(lang, "menu.file.start")),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "menu.file.start"));
                }
                if ui
                    .add_enabled(
                        has,
                        egui::Button::new(t(lang, "menu.file.stop")),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "menu.file.stop"));
                }
                if ui
                    .add_enabled(has, egui::Button::new(t(lang, "menu.file.redownload")))
                    .clicked()
                {
                    self.soon(t(lang, "menu.file.redownload"));
                }
                ui.separator();
                if ui
                    .add_enabled(has, egui::Button::new(t(lang, "menu.file.remove")))
                    .clicked()
                {
                    self.remove_selected();
                }
            });

            // ===================== Téléchargement =====================
            let it_download = ui.menu_button(t(lang, "menu.download.title"), |ui| {
                if ui.button(t(lang, "menu.download.pause_all")).clicked() {
                    self.soon(t(lang, "menu.download.pause_all"));
                }
                if ui.button(t(lang, "menu.download.stop_all")).clicked() {
                    self.soon(t(lang, "menu.download.stop_all"));
                }
                if ui
                    .button(t(lang, "menu.download.remove_completed"))
                    .clicked()
                {
                    self.remove_completed();
                }
                if ui
                    .add(egui::Button::new(t(lang, "menu.download.search")).shortcut_text("Ctrl+F"))
                    .clicked()
                {
                    self.search_open = !self.search_open;
                }

                ui.separator();
                if ui.button(t(lang, "menu.download.schedule")).clicked() {
                    self.soon(t(lang, "menu.download.schedule"));
                }
                ui.menu_button(t(lang, "menu.download.start_queue"), |ui| {
                    if ui.button(t(lang, "menu.download.queue_main")).clicked() {
                        self.soon(t(lang, "menu.download.queue_main"));
                    }
                    if ui.button(t(lang, "menu.download.queue_sync")).clicked() {
                        self.soon(t(lang, "menu.download.queue_sync"));
                    }
                });
                ui.menu_button(t(lang, "menu.download.stop_queue"), |ui| {
                    if ui.button(t(lang, "menu.download.queue_main")).clicked() {
                        self.soon(t(lang, "menu.download.queue_main"));
                    }
                    if ui.button(t(lang, "menu.download.queue_sync")).clicked() {
                        self.soon(t(lang, "menu.download.queue_sync"));
                    }
                });
                ui.menu_button(t(lang, "menu.download.limiter"), |ui| {
                    if ui.radio(self.speed_limit_enabled, t(lang, "menu.download.limiter_enable")).clicked() {
                        self.speed_limit_enabled = true;
                        self.set_status(t(lang, "status.limiter_enabled"));
                    }
                    if ui
                        .radio(!self.speed_limit_enabled, t(lang, "menu.download.limiter_disable"))
                        .clicked()
                    {
                        self.speed_limit_enabled = false;
                        self.set_status(t(lang, "status.limiter_disabled"));
                    }
                    if ui.button(t(lang, "menu.download.settings")).clicked() {
                        self.soon(t(lang, "menu.download.settings_soon"));
                    }
                });
                if ui.button(t(lang, "menu.download.booster")).clicked() {
                    self.soon(t(lang, "menu.download.booster"));
                }
            });

            // ===================== Affichage =====================
            let it_view = ui.menu_button(t(lang, "menu.view.title"), |ui| {
                ui.checkbox(
                    &mut self.show_categories,
                    t(lang, "menu.view.categories"),
                );

                ui.menu_button(t(lang, "menu.view.sort"), |ui| {
                    let opts: &[(SortBy, &str)] = &[
                        (SortBy::DateAdded, "sort.date_added"),
                        (SortBy::Name, "sort.name"),
                        (SortBy::Size, "sort.size"),
                        (SortBy::Status, "sort.status"),
                        (SortBy::TimeLeft, "sort.time_left"),
                        (SortBy::Speed, "sort.speed"),
                        (SortBy::LastTry, "sort.last_try"),
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

                ui.menu_button(t(lang, "menu.view.toolbar"), |ui| {
                    if ui.button(t(lang, "menu.view.toolbar_customize")).clicked() {
                        self.soon(t(lang, "menu.view.toolbar_customize_soon"));
                    }
                    ui.radio_value(&mut self.toolbar_big, true, t(lang, "menu.view.big_buttons"));
                    ui.radio_value(
                        &mut self.toolbar_big,
                        false,
                        t(lang, "menu.view.small_buttons"),
                    );
                    if ui.button(t(lang, "menu.view.interface")).clicked() {
                        self.soon(t(lang, "menu.view.interface_soon"));
                    }
                    if ui.button(t(lang, "menu.view.shortcuts")).clicked() {
                        self.soon(t(lang, "menu.view.shortcuts_soon"));
                    }
                });

                ui.checkbox(
                    &mut self.notifications_enabled,
                    t(lang, "menu.view.notifications"),
                );
                if ui
                    .button(t(lang, "menu.view.columns"))
                    .clicked()
                {
                    self.soon(t(lang, "menu.view.columns_soon"));
                }

                ui.menu_button(t(lang, "menu.view.mode"), |ui| {
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

                ui.menu_button(t(lang, "menu.view.font"), |ui| {
                    if ui.button(t(lang, "menu.view.font_select")).clicked() {
                        self.soon(t(lang, "menu.view.font_select_soon"));
                    }
                    if ui
                        .button(t(lang, "menu.view.font_reset"))
                        .clicked()
                    {
                        self.soon(t(lang, "menu.view.font_reset_soon"));
                    }
                });

                ui.menu_button(t(lang, "menu.view.language"), |ui| {
                    ui.radio_value(&mut self.lang, Lang::En, Lang::En.native_name());
                    ui.radio_value(&mut self.lang, Lang::Fr, Lang::Fr.native_name());
                });
            });

            // ===================== Aide =====================
            let it_help = ui.menu_button(t(lang, "menu.help.title"), |ui| {
                if ui.add(egui::Button::new(t(lang, "menu.help.title")).shortcut_text("F1")).clicked() {
                    self.show_help = true;
                }
                if ui.button(t(lang, "menu.help.update")).clicked() {
                    self.soon(t(lang, "menu.help.update"));
                }
                ui.menu_button(t(lang, "menu.help.about"), |ui| {
                    if ui.button(t(lang, "menu.help.about")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "menu.help.authors")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "menu.help.license")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "menu.help.credits")).clicked() {
                        self.show_about = true;
                    }
                });
            });

            // Comportement barre de menus façon IDM : un menu étant déjà ouvert, survoler
            // un autre menu du haut l'ouvre (plus besoin de cliquer dessus).
            for it in [&it_tasks, &it_file, &it_download, &it_view, &it_help] {
                let id = egui::Popup::default_response_id(&it.response);
                if it.response.hovered()
                    && egui::Popup::is_any_open(&ctx)
                    && !egui::Popup::is_id_open(&ctx, id)
                {
                    egui::Popup::open_id(&ctx, id);
                }
            }
        });
    }

    /// Items d'un sous-menu Exporter ou Importer : un bouton par clé de format fournie.
    fn export_import_items(&mut self, ui: &mut egui::Ui, lang: Lang, verb: &str, keys: &[&'static str]) {
        for key in keys {
            let f = t(lang, key);
            if ui.button(f).clicked() {
                self.soon(&format!("{verb} {f}"));
            }
        }
    }
}

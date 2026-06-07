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
            ui.menu_button(t(lang, "Tâches", "Tasks"), |ui| {
                if ui
                    .button(t(lang, "Ajouter nouveau téléchargement", "Add new download"))
                    .clicked()
                {
                    self.show_add = true;
                }
                if ui
                    .button(t(lang, "Téléchargement manuel", "Manual download"))
                    .clicked()
                {
                    self.show_manual = true;
                }
                if ui
                    .add(
                        egui::Button::new(t(
                            lang,
                            "Téléchargement par lot (presse-papier)",
                            "Batch download (clipboard)",
                        ))
                        .shortcut_text("Ctrl+Maj+V"),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "Lot depuis le presse-papier", "Batch from clipboard"));
                }

                ui.separator();
                ui.menu_button(t(lang, "Exporter", "Export"), |ui| {
                    self.export_import_items(ui, lang, t(lang, "Export", "Export"));
                });
                ui.menu_button(t(lang, "Importer", "Import"), |ui| {
                    self.export_import_items(ui, lang, t(lang, "Import", "Import"));
                });

                ui.separator();
                if ui.button(t(lang, "Quitter", "Quit")).clicked() {
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });

            // ===================== Fichier =====================
            // Items contextuels : actifs seulement si une ligne est sélectionnée.
            ui.menu_button(t(lang, "Fichier", "File"), |ui| {
                let has = self.selected.is_some();
                if ui
                    .add_enabled(
                        has,
                        egui::Button::new(t(lang, "Démarrer le téléchargement", "Start download")),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "Démarrer le téléchargement", "Start download"));
                }
                if ui
                    .add_enabled(
                        has,
                        egui::Button::new(t(lang, "Arrêter le téléchargement", "Stop download")),
                    )
                    .clicked()
                {
                    self.soon(t(lang, "Arrêter le téléchargement", "Stop download"));
                }
                if ui
                    .add_enabled(has, egui::Button::new(t(lang, "Re-télécharger", "Re-download")))
                    .clicked()
                {
                    self.soon(t(lang, "Re-télécharger", "Re-download"));
                }
                ui.separator();
                if ui
                    .add_enabled(has, egui::Button::new(t(lang, "Supprimer", "Remove")))
                    .clicked()
                {
                    self.remove_selected();
                }
            });

            // ===================== Téléchargement =====================
            ui.menu_button(t(lang, "Téléchargement", "Download"), |ui| {
                if ui.button(t(lang, "Tout mettre en pause", "Pause all")).clicked() {
                    self.soon(t(lang, "Tout mettre en pause", "Pause all"));
                }
                if ui.button(t(lang, "Tout arrêter", "Stop all")).clicked() {
                    self.soon(t(lang, "Tout arrêter", "Stop all"));
                }
                if ui
                    .button(t(lang, "Supprimer les terminés", "Remove completed"))
                    .clicked()
                {
                    self.remove_completed();
                }
                if ui
                    .add(egui::Button::new(t(lang, "Rechercher", "Search")).shortcut_text("Ctrl+F"))
                    .clicked()
                {
                    self.search_open = !self.search_open;
                }

                ui.separator();
                if ui.button(t(lang, "Planifier", "Schedule")).clicked() {
                    self.soon(t(lang, "Planifier", "Schedule"));
                }
                ui.menu_button(t(lang, "Démarrer file d'attente", "Start queue"), |ui| {
                    if ui.button(t(lang, "File principale", "Main queue")).clicked() {
                        self.soon(t(lang, "File principale", "Main queue"));
                    }
                    if ui.button(t(lang, "File sync", "Sync queue")).clicked() {
                        self.soon(t(lang, "File sync", "Sync queue"));
                    }
                });
                ui.menu_button(t(lang, "Arrêter file d'attente", "Stop queue"), |ui| {
                    if ui.button(t(lang, "File principale", "Main queue")).clicked() {
                        self.soon(t(lang, "File principale", "Main queue"));
                    }
                    if ui.button(t(lang, "File sync", "Sync queue")).clicked() {
                        self.soon(t(lang, "File sync", "Sync queue"));
                    }
                });
                ui.menu_button(t(lang, "Limiteur de vitesse", "Speed limiter"), |ui| {
                    if ui.radio(self.speed_limit_enabled, t(lang, "Activer", "Enable")).clicked() {
                        self.speed_limit_enabled = true;
                        self.set_status(t(lang, "Limiteur activé", "Limiter enabled"));
                    }
                    if ui
                        .radio(!self.speed_limit_enabled, t(lang, "Désactiver", "Disable"))
                        .clicked()
                    {
                        self.speed_limit_enabled = false;
                        self.set_status(t(lang, "Limiteur désactivé", "Limiter disabled"));
                    }
                    if ui.button(t(lang, "Paramètres…", "Settings…")).clicked() {
                        self.soon(t(lang, "Paramètres du limiteur", "Limiter settings"));
                    }
                });
                if ui.button(t(lang, "Booster de vitesse", "Speed booster")).clicked() {
                    self.soon(t(lang, "Booster de vitesse", "Speed booster"));
                }
            });

            // ===================== Affichage =====================
            ui.menu_button(t(lang, "Affichage", "View"), |ui| {
                ui.checkbox(
                    &mut self.show_categories,
                    t(lang, "Panneau Catégories", "Categories panel"),
                );

                ui.menu_button(t(lang, "Classer les fichiers", "Sort files"), |ui| {
                    let opts: &[(SortBy, &str, &str)] = &[
                        (SortBy::DateAdded, "Par ordre d'ajout", "By date added"),
                        (SortBy::Name, "Par nom de fichier", "By file name"),
                        (SortBy::Size, "Par taille", "By size"),
                        (SortBy::Status, "Par statut", "By status"),
                        (SortBy::TimeLeft, "Par temps restant", "By time left"),
                        (SortBy::Speed, "Par vitesse", "By speed"),
                        (SortBy::LastTry, "Par date du dernier essai", "By last try"),
                        (SortBy::Description, "Par description", "By description"),
                        (SortBy::Location, "Par emplacement", "By location"),
                        (SortBy::Address, "Par adresse", "By address"),
                        (SortBy::ParentPage, "Par page web parente", "By parent page"),
                    ];
                    for (variant, fr, en) in opts {
                        if ui.radio_value(&mut self.sort_by, *variant, t(lang, fr, en)).clicked() {
                            self.sort_downloads();
                        }
                    }
                });

                ui.menu_button(t(lang, "Barre d'outils", "Toolbar"), |ui| {
                    if ui.button(t(lang, "Personnaliser…", "Customize…")).clicked() {
                        self.soon(t(lang, "Personnaliser la barre d'outils", "Customize toolbar"));
                    }
                    ui.radio_value(&mut self.toolbar_big, true, t(lang, "Grands boutons", "Big buttons"));
                    ui.radio_value(
                        &mut self.toolbar_big,
                        false,
                        t(lang, "Petits boutons", "Small buttons"),
                    );
                    if ui.button(t(lang, "Interface…", "Interface…")).clicked() {
                        self.soon(t(lang, "Interface", "Interface"));
                    }
                    if ui.button(t(lang, "Raccourcis…", "Shortcuts…")).clicked() {
                        self.soon(t(lang, "Raccourcis", "Shortcuts"));
                    }
                });

                ui.checkbox(
                    &mut self.notifications_enabled,
                    t(lang, "Notifications", "Notifications"),
                );
                if ui
                    .button(t(lang, "Personnaliser les colonnes…", "Customize columns…"))
                    .clicked()
                {
                    self.soon(t(lang, "Colonnes affichées", "Displayed columns"));
                }

                ui.menu_button(t(lang, "Mode", "Mode"), |ui| {
                    let modes: &[(ThemeMode, &str, &str)] = &[
                        (ThemeMode::Dark, "Sombre", "Dark"),
                        (ThemeMode::Light, "Claire", "Light"),
                        (ThemeMode::System, "Système", "System"),
                    ];
                    for (variant, fr, en) in modes {
                        if ui.radio_value(&mut self.theme, *variant, t(lang, fr, en)).clicked() {
                            self.apply_theme(&ctx);
                        }
                    }
                });

                ui.menu_button(t(lang, "Police", "Font"), |ui| {
                    if ui.button(t(lang, "Sélectionner la police…", "Select font…")).clicked() {
                        self.soon(t(lang, "Sélection de police", "Font selection"));
                    }
                    if ui
                        .button(t(lang, "Rétablir la police par défaut", "Reset default font"))
                        .clicked()
                    {
                        self.soon(t(lang, "Police par défaut", "Default font"));
                    }
                });

                ui.menu_button(t(lang, "Langue", "Language"), |ui| {
                    ui.radio_value(&mut self.lang, Lang::En, Lang::En.native_name());
                    ui.radio_value(&mut self.lang, Lang::Fr, Lang::Fr.native_name());
                });
            });

            // ===================== Aide =====================
            ui.menu_button(t(lang, "Aide", "Help"), |ui| {
                if ui.add(egui::Button::new(t(lang, "Aide", "Help")).shortcut_text("F1")).clicked() {
                    self.show_help = true;
                }
                if ui.button(t(lang, "Mise à jour rapide", "Quick update")).clicked() {
                    self.soon(t(lang, "Mise à jour rapide", "Quick update"));
                }
                ui.menu_button(t(lang, "À propos", "About"), |ui| {
                    if ui.button(t(lang, "À propos", "About")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "Auteurs", "Authors")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "Licence", "License")).clicked() {
                        self.show_about = true;
                    }
                    if ui.button(t(lang, "Crédits", "Credits")).clicked() {
                        self.show_about = true;
                    }
                });
            });
        });
    }

    /// Items partagés des sous-menus Exporter / Importer (4 formats).
    fn export_import_items(&mut self, ui: &mut egui::Ui, lang: Lang, verb: &str) {
        let formats = [
            t(lang, "Fichier .adm (notre format)", ".adm file (our format)"),
            t(lang, "Fichier texte (.txt)", "Text file (.txt)"),
            t(lang, "Fichier JSON (.json)", "JSON file (.json)"),
            t(lang, "Fichier Excel (.xlsx)", "Excel file (.xlsx)"),
        ];
        for f in formats {
            if ui.button(f).clicked() {
                self.soon(&format!("{verb} {f}"));
            }
        }
    }
}

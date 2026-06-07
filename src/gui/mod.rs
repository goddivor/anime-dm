//! Couche interface utilisateur (egui) : tout le code lié à l'UX vit ici.
//!
//! - [`app`]  : l'application egui (état, panneaux, dialogues, boucle de rendu)
//! - [`menu`] : la barre de menus type IDM (Tâches / Fichier / Téléchargement / Affichage / Aide)
//! - [`i18n`] : traduction FR / EN

mod app;
mod menu;
pub mod i18n;

pub use app::App;

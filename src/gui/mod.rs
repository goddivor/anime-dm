//! Couche interface utilisateur (egui) : tout le code lié à l'UX vit ici.
//!
//! - [`app`]     : état de l'application, actions partagées, boucle de rendu
//! - [`menu`]    : barre de menus type IDM (Tâches / Fichier / Téléchargement / Affichage / Aide)
//! - [`view`]    : barre d'outils, sidebar des catégories, table, barre d'état
//! - [`dialogs`] : fenêtres modales (ajout, à propos, aide, manuel)
//! - [`i18n`]    : traduction FR / EN

mod app;
mod dialogs;
pub mod i18n;
mod menu;
mod view;

pub use app::App;

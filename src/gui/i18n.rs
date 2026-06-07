//! Internationalisation : **une clé par texte, une source par langue**.
//!
//! Les traductions vivent dans `locales/<langue>.json` (fichiers plats `clé: valeur`),
//! embarqués à la compilation et chargés à la première utilisation.
//!
//! **Ajouter une langue** = (1) créer `locales/xx.json` en copiant les clés, (2) ajouter une
//! variante à [`Lang`] + son `include_str!`. Aucun changement dans les ~150 points d'appel.

use std::collections::HashMap;
use std::sync::OnceLock;

/// Langue de l'interface.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Lang {
    Fr,
    En,
}

impl Lang {
    /// Nom de la langue dans sa propre langue (pour le menu Langue).
    pub fn native_name(self) -> &'static str {
        match self {
            Lang::Fr => "Français",
            Lang::En => "English",
        }
    }

    /// Contenu JSON de la locale, embarqué à la compilation.
    fn source(self) -> &'static str {
        match self {
            Lang::Fr => include_str!("../../locales/fr.json"),
            Lang::En => include_str!("../../locales/en.json"),
        }
    }
}

/// Table `clé -> texte` d'une langue, désérialisée une seule fois puis mise en cache.
fn table(lang: Lang) -> &'static HashMap<String, String> {
    static FR: OnceLock<HashMap<String, String>> = OnceLock::new();
    static EN: OnceLock<HashMap<String, String>> = OnceLock::new();
    let cell = match lang {
        Lang::Fr => &FR,
        Lang::En => &EN,
    };
    cell.get_or_init(|| {
        serde_json::from_str(lang.source()).expect("fichier de locale JSON invalide")
    })
}

/// Traduit une clé dans la langue donnée.
///
/// Repli en cascade : langue demandée → anglais → la clé brute (rend visible une traduction
/// manquante sans planter).
pub fn t(lang: Lang, key: &'static str) -> &'static str {
    if let Some(v) = table(lang).get(key) {
        return v.as_str();
    }
    if let Some(v) = table(Lang::En).get(key) {
        return v.as_str();
    }
    key
}

//! Internationalisation (FR / EN).
//!
//! Approche volontairement simple et sans table centrale : on passe les deux variantes
//! au point d'appel via [`t`]. Lisible, et trivial à étendre (ajouter une 3ᵉ langue = un
//! nouveau bras de `match`).

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
}

/// Retourne la variante correspondant à la langue courante.
pub fn t<'a>(lang: Lang, fr: &'a str, en: &'a str) -> &'a str {
    match lang {
        Lang::Fr => fr,
        Lang::En => en,
    }
}

use std::collections::HashMap;
use std::sync::OnceLock;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Lang {
    Fr,
    En,
}

impl Lang {
    pub fn native_name(self) -> &'static str {
        match self {
            Lang::Fr => "Français",
            Lang::En => "English",
        }
    }

    fn source(self) -> &'static str {
        match self {
            Lang::Fr => include_str!("../../locales/fr.json"),
            Lang::En => include_str!("../../locales/en.json"),
        }
    }
}

fn table(lang: Lang) -> &'static HashMap<String, String> {
    static FR: OnceLock<HashMap<String, String>> = OnceLock::new();
    static EN: OnceLock<HashMap<String, String>> = OnceLock::new();
    let cell = match lang {
        Lang::Fr => &FR,
        Lang::En => &EN,
    };
    cell.get_or_init(|| {
        serde_json::from_str(lang.source()).expect("invalid locale JSON file")
    })
}

pub fn t(lang: Lang, key: &'static str) -> &'static str {
    if let Some(v) = table(lang).get(key) {
        return v.as_str();
    }
    if let Some(v) = table(Lang::En).get(key) {
        return v.as_str();
    }
    key
}

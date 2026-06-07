//! Analyse d'une sélection d'épisodes saisie par l'utilisateur.
//!
//! Formats acceptés : `1-20`, `1,5,8`, `1-3,7,10-12`, vide / `tous` / `all` -> tout.

/// Renvoie les numéros d'épisodes (1-based), triés et dédupliqués, bornés à `[1, max]`.
pub fn parse(input: &str, max: usize) -> Vec<usize> {
    let trimmed = input.trim().to_lowercase();
    if max == 0 {
        return Vec::new();
    }
    if trimmed.is_empty() || trimmed == "tous" || trimmed == "all" || trimmed == "*" {
        return (1..=max).collect();
    }

    let mut set = std::collections::BTreeSet::new();
    for token in trimmed.split(',') {
        let token = token.trim();
        if token.is_empty() {
            continue;
        }
        if let Some((a, b)) = token.split_once('-') {
            if let (Ok(mut a), Ok(mut b)) = (a.trim().parse::<usize>(), b.trim().parse::<usize>()) {
                if a > b {
                    std::mem::swap(&mut a, &mut b);
                }
                for n in a..=b {
                    if (1..=max).contains(&n) {
                        set.insert(n);
                    }
                }
            }
        } else if let Ok(n) = token.parse::<usize>() {
            if (1..=max).contains(&n) {
                set.insert(n);
            }
        }
    }
    set.into_iter().collect()
}

#[cfg(test)]
mod tests {
    use super::parse;

    #[test]
    fn ranges_and_lists() {
        assert_eq!(parse("1-5", 10), vec![1, 2, 3, 4, 5]);
        assert_eq!(parse("1,5,8", 10), vec![1, 5, 8]);
        assert_eq!(parse("1-3,7,10-12", 10), vec![1, 2, 3, 7, 10]);
        assert_eq!(parse("", 3), vec![1, 2, 3]);
        assert_eq!(parse("tous", 2), vec![1, 2]);
        assert_eq!(parse("5-1", 10), vec![1, 2, 3, 4, 5]);
        assert_eq!(parse("99", 10), Vec::<usize>::new());
    }
}

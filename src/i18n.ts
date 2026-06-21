import frJson from "./locales/fr.json";
import enJson from "./locales/en.json";

export type Lang = "fr" | "en";

const fr = frJson as unknown as Record<string, string>;
const en = enJson as unknown as Record<string, string>;
const tables: Record<Lang, Record<string, string>> = { fr, en };

export type T = (key: string) => string;

export function translator(lang: Lang): T {
  const tbl = tables[lang];
  return (key: string) => tbl[key] ?? en[key] ?? key;
}

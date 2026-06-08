#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod gui;
mod model;
mod selection;
mod selftest;
mod worker;

use eframe::egui;

fn main() -> eframe::Result<()> {
    if std::env::args().any(|a| a == "--selftest") {
        std::process::exit(selftest::run());
    }

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1000.0, 640.0])
            .with_min_inner_size([760.0, 460.0])
            .with_title("Anime Download Manager"),
        ..Default::default()
    };

    eframe::run_native(
        "Anime DM",
        options,
        Box::new(|cc| Ok(Box::new(gui::App::new(cc)))),
    )
}

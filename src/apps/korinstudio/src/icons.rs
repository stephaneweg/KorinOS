// Les icônes de KorinStudio, dessinées au trait (style « codicons » de VS Code), à toute taille.

use egui::{Color32, Painter, Pos2, Rect, Response, Sense, Shape, Stroke, Ui, Vec2, pos2, vec2};
use std::f32::consts::TAU;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Icon {
    Files,
    Search,
    Git,
    Run,
    Copilot,
    Gear,
    Close,
    Dot,
    Folder,
    FolderOpen,
    File,
    ChevronRight,
    ChevronDown,
    Play,
    Stop,
    Build,
    Error,
    Warning,
    Info,
    Check,
    Plus,
    Refresh,
    Collapse,
    Send,
    Bell,
    Branch,
    NewFile,
    NewFolder,
    Terminal,
    Trash,
    Sparkle,
    Person,
    Tool,
    Split,
    Ellipsis,
    Clear,
    Layout,
    Spinner(u8),
}

fn s(w: f32, c: Color32) -> Stroke {
    Stroke::new(w, c)
}

fn poly(p: &Painter, pts: Vec<Pos2>, st: Stroke) {
    p.add(Shape::line(pts, st));
}

/// Dessine l'icône dans `r` (carrée de préférence).
pub fn paint(p: &Painter, r: Rect, icon: Icon, col: Color32) {
    let k = r.width().min(r.height()) / 16.0; // grille de 16
    let o = r.center() - vec2(8.0 * k, 8.0 * k);
    let q = |x: f32, y: f32| o + vec2(x * k, y * k);
    let w = (1.2 * k).max(1.0);
    let st = s(w, col);
    match icon {
        Icon::Files => {
            poly(p, vec![q(5.0, 3.5), q(10.5, 3.5), q(13.5, 6.5), q(13.5, 14.5), q(5.0, 14.5), q(5.0, 3.5)], st);
            poly(p, vec![q(10.5, 3.5), q(10.5, 6.5), q(13.5, 6.5)], st);
            poly(p, vec![q(3.0, 5.5), q(3.0, 16.0), q(11.0, 16.0)], st);
        }
        Icon::Search => {
            p.circle_stroke(q(9.5, 6.5), 4.5 * k, st);
            p.line_segment([q(6.3, 9.7), q(2.0, 14.0)], s(w * 1.4, col));
        }
        Icon::Git | Icon::Branch => {
            p.circle_stroke(q(5.0, 3.5), 1.8 * k, st);
            p.circle_stroke(q(5.0, 13.0), 1.8 * k, st);
            p.circle_stroke(q(12.0, 5.5), 1.8 * k, st);
            p.line_segment([q(5.0, 5.3), q(5.0, 11.2)], st);
            p.add(Shape::CubicBezier(egui::epaint::CubicBezierShape::from_points_stroke(
                [q(12.0, 7.3), q(12.0, 10.0), q(5.0, 9.0), q(5.0, 11.2)],
                false,
                Color32::TRANSPARENT,
                st,
            )));
        }
        Icon::Run => {
            poly(p, vec![q(4.0, 2.5), q(13.0, 8.0), q(4.0, 13.5), q(4.0, 2.5)], st);
            p.circle_filled(q(12.5, 12.5), 2.6 * k, col);
        }
        Icon::Copilot | Icon::Sparkle => {
            sparkle(p, q(7.0, 8.5), 6.0 * k, col);
            sparkle(p, q(13.0, 3.5), 2.6 * k, col);
        }
        Icon::Gear => {
            let c0 = q(8.0, 8.0);
            p.circle_stroke(c0, 2.3 * k, st);
            let mut pts = Vec::new();
            for i in 0..=48 {
                let a = i as f32 / 48.0 * TAU;
                let tooth = ((a * 8.0 / TAU * 2.0).floor() as i32 % 2 == 0) as i32 as f32;
                let rr = (5.0 + 1.6 * tooth) * k;
                pts.push(c0 + vec2(a.cos(), a.sin()) * rr);
            }
            poly(p, pts, st);
        }
        Icon::Close => {
            p.line_segment([q(4.0, 4.0), q(12.0, 12.0)], st);
            p.line_segment([q(12.0, 4.0), q(4.0, 12.0)], st);
        }
        Icon::Dot => {
            p.circle_filled(q(8.0, 8.0), 4.0 * k, col);
        }
        Icon::Folder | Icon::FolderOpen => {
            poly(p, vec![q(1.5, 3.0), q(6.0, 3.0), q(7.5, 4.5), q(14.5, 4.5), q(14.5, 13.5), q(1.5, 13.5), q(1.5, 3.0)], st);
            if icon == Icon::FolderOpen {
                poly(p, vec![q(1.5, 13.5), q(4.0, 7.5), q(15.5, 7.5), q(13.0, 13.5)], st);
            }
        }
        Icon::File => {
            poly(p, vec![q(3.5, 1.5), q(9.5, 1.5), q(12.5, 4.5), q(12.5, 14.5), q(3.5, 14.5), q(3.5, 1.5)], st);
            poly(p, vec![q(9.5, 1.5), q(9.5, 4.5), q(12.5, 4.5)], st);
        }
        Icon::ChevronRight => poly(p, vec![q(6.0, 3.5), q(10.5, 8.0), q(6.0, 12.5)], st),
        Icon::ChevronDown => poly(p, vec![q(3.5, 6.0), q(8.0, 10.5), q(12.5, 6.0)], st),
        Icon::Play => {
            p.add(Shape::convex_polygon(vec![q(4.5, 2.5), q(13.0, 8.0), q(4.5, 13.5)], col, Stroke::NONE));
        }
        Icon::Stop => {
            p.rect_filled(Rect::from_min_max(q(3.5, 3.5), q(12.5, 12.5)), 1.0, col);
        }
        Icon::Build => {
            // un marteau
            p.line_segment([q(3.0, 14.0), q(9.5, 7.5)], s(w * 1.6, col));
            poly(p, vec![q(7.5, 3.5), q(10.5, 1.5), q(14.5, 5.5), q(12.5, 8.5), q(7.5, 3.5)], st);
        }
        Icon::Error => {
            p.circle_stroke(q(8.0, 8.0), 6.5 * k, st);
            p.line_segment([q(5.5, 5.5), q(10.5, 10.5)], st);
            p.line_segment([q(10.5, 5.5), q(5.5, 10.5)], st);
        }
        Icon::Warning => {
            poly(p, vec![q(8.0, 1.5), q(15.0, 14.0), q(1.0, 14.0), q(8.0, 1.5)], st);
            p.line_segment([q(8.0, 5.5), q(8.0, 10.0)], st);
            p.circle_filled(q(8.0, 12.0), 0.8 * k, col);
        }
        Icon::Info => {
            p.circle_stroke(q(8.0, 8.0), 6.5 * k, st);
            p.line_segment([q(8.0, 7.0), q(8.0, 11.5)], st);
            p.circle_filled(q(8.0, 4.8), 0.8 * k, col);
        }
        Icon::Check => poly(p, vec![q(2.5, 8.5), q(6.0, 12.0), q(13.5, 4.0)], s(w * 1.2, col)),
        Icon::Plus => {
            p.line_segment([q(8.0, 2.5), q(8.0, 13.5)], st);
            p.line_segment([q(2.5, 8.0), q(13.5, 8.0)], st);
        }
        Icon::Refresh => {
            let c0 = q(8.0, 8.0);
            let pts: Vec<Pos2> = (0..=24).map(|i| {
                let a = -0.9 + i as f32 / 24.0 * (TAU * 0.82);
                c0 + vec2(a.cos(), a.sin()) * 5.5 * k
            }).collect();
            let end = *pts.last().unwrap();
            poly(p, pts, st);
            poly(p, vec![end + vec2(-3.0 * k, 0.0), end, end + vec2(0.0, -3.0 * k)], st);
        }
        Icon::Collapse => {
            p.rect_stroke(Rect::from_min_max(q(2.5, 2.5), q(13.5, 13.5)), 1.0, st, egui::StrokeKind::Middle);
            p.line_segment([q(5.5, 8.0), q(10.5, 8.0)], st);
        }
        Icon::Send => {
            p.add(Shape::convex_polygon(vec![q(1.5, 2.5), q(15.0, 8.0), q(1.5, 13.5), q(4.0, 8.0)], col, Stroke::NONE));
        }
        Icon::Bell => {
            poly(p, vec![q(3.0, 12.0), q(4.0, 10.5), q(4.0, 6.5), q(6.0, 3.5), q(10.0, 3.5), q(12.0, 6.5), q(12.0, 10.5), q(13.0, 12.0), q(3.0, 12.0)], st);
            p.line_segment([q(6.8, 14.0), q(9.2, 14.0)], st);
        }
        Icon::NewFile => {
            paint(p, r, Icon::File, col);
            p.line_segment([q(8.0, 7.0), q(8.0, 12.0)], st);
            p.line_segment([q(5.5, 9.5), q(10.5, 9.5)], st);
        }
        Icon::NewFolder => {
            paint(p, r, Icon::Folder, col);
            p.line_segment([q(8.0, 6.5), q(8.0, 11.5)], st);
            p.line_segment([q(5.5, 9.0), q(10.5, 9.0)], st);
        }
        Icon::Terminal => {
            p.rect_stroke(Rect::from_min_max(q(1.5, 2.5), q(14.5, 13.5)), 1.5 * k, st, egui::StrokeKind::Middle);
            poly(p, vec![q(4.0, 5.5), q(6.5, 8.0), q(4.0, 10.5)], st);
            p.line_segment([q(8.0, 10.5), q(12.0, 10.5)], st);
        }
        Icon::Trash => {
            p.line_segment([q(2.5, 4.0), q(13.5, 4.0)], st);
            poly(p, vec![q(4.0, 4.0), q(5.0, 14.5), q(11.0, 14.5), q(12.0, 4.0)], st);
            poly(p, vec![q(6.0, 4.0), q(6.5, 2.0), q(9.5, 2.0), q(10.0, 4.0)], st);
        }
        Icon::Person => {
            p.circle_stroke(q(8.0, 5.0), 3.0 * k, st);
            poly(p, vec![q(2.5, 15.0), q(3.5, 11.0), q(6.0, 9.5), q(10.0, 9.5), q(12.5, 11.0), q(13.5, 15.0)], st);
        }
        Icon::Tool => {
            p.line_segment([q(2.5, 13.5), q(8.5, 7.5)], s(w * 1.5, col));
            p.circle_stroke(q(10.5, 5.5), 3.2 * k, st);
        }
        Icon::Split => {
            p.rect_stroke(Rect::from_min_max(q(1.5, 2.5), q(14.5, 13.5)), 1.0, st, egui::StrokeKind::Middle);
            p.line_segment([q(8.0, 2.5), q(8.0, 13.5)], st);
        }
        Icon::Layout => {
            p.rect_stroke(Rect::from_min_max(q(1.5, 2.5), q(14.5, 13.5)), 1.0, st, egui::StrokeKind::Middle);
            p.line_segment([q(1.5, 9.5), q(14.5, 9.5)], st);
        }
        Icon::Ellipsis => {
            for x in [3.5, 8.0, 12.5] {
                p.circle_filled(q(x, 8.0), 1.1 * k, col);
            }
        }
        Icon::Clear => {
            poly(p, vec![q(2.0, 12.5), q(8.0, 3.0), q(14.0, 3.0), q(8.0, 12.5), q(2.0, 12.5)], st);
            p.line_segment([q(1.5, 14.5), q(14.5, 14.5)], st);
        }
        Icon::Spinner(phase) => {
            let c0 = q(8.0, 8.0);
            let a0 = phase as f32 / 16.0 * TAU;
            let pts: Vec<Pos2> = (0..=16).map(|i| {
                let a = a0 + i as f32 / 16.0 * TAU * 0.7;
                c0 + vec2(a.cos(), a.sin()) * 5.5 * k
            }).collect();
            poly(p, pts, s(w * 1.2, col));
        }
    }
}

fn sparkle(p: &Painter, c0: Pos2, r: f32, col: Color32) {
    let i = r * 0.22;
    let pts = vec![
        c0 + vec2(0.0, -r),
        c0 + vec2(i, -i),
        c0 + vec2(r, 0.0),
        c0 + vec2(i, i),
        c0 + vec2(0.0, r),
        c0 + vec2(-i, i),
        c0 + vec2(-r, 0.0),
        c0 + vec2(-i, -i),
    ];
    p.add(Shape::convex_polygon(pts, col, Stroke::NONE));
}

/// Un bouton-icône sans cadre (survol discret, comme les actions de VS Code), avec bulle d'aide.
pub fn button(ui: &mut Ui, icon: Icon, size: f32, tip: &str) -> Response {
    let (rect, resp) = ui.allocate_exact_size(Vec2::splat(size), Sense::click());
    let hovered = resp.hovered();
    let col = if hovered { ui.visuals().strong_text_color() } else { ui.visuals().text_color() };
    if hovered {
        ui.painter().rect_filled(rect, 4.0, ui.visuals().widgets.hovered.bg_fill);
    }
    let pad = size * 0.2;
    paint(ui.painter(), rect.shrink(pad), icon, col);
    if tip.is_empty() { resp } else { resp.on_hover_text(tip) }
}

/// Une icône seule, alignée sur le texte.
pub fn inline(ui: &mut Ui, icon: Icon, size: f32, col: Color32) -> Response {
    let (rect, resp) = ui.allocate_exact_size(Vec2::splat(size), Sense::hover());
    paint(ui.painter(), rect, icon, col);
    resp
}

pub fn spinner_phase(ui: &Ui) -> u8 {
    ui.ctx().request_repaint_after(std::time::Duration::from_millis(60));
    ((ui.input(|i| i.time) * 16.0) as u64 % 16) as u8
}

#[allow(dead_code)]
pub fn at(p: &Painter, center: Pos2, size: f32, icon: Icon, col: Color32) {
    paint(p, Rect::from_center_size(center, vec2(size, size)), icon, col);
}

#[allow(dead_code)]
pub fn origin() -> Pos2 {
    pos2(0.0, 0.0)
}

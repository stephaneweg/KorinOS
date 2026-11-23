//! Les quotas de l'abonnement en bas du rail (`layout/quota_meter.rs`) : 5 h et 7 jours (barres, pourcentages,
//! l'heure de remise à zéro), les modèles, les crédits, l'avertissement près de la limite ; « Détail » demande à la
//! CLI la répartition de la semaine (`refresh_usage`).

use ccui_shared::usage::UsageRow;
use ccui_shared::{fmt, i18n, t};
use egui::{Color32, RichText, Sense, Ui};

use crate::app::Cx;
use crate::theme::{self, Tokens};
use crate::widgets;

#[derive(Default)]
pub struct Quota {
    open: bool,
    loading: bool,
    task: Option<crate::backend::Task<Result<(), String>>>,
    error: Option<String>,
}

/// Une ligne : nom, barre, pourcentage.
fn bar_row(ui: &mut Ui, t: &Tokens, name: &str, f: f64) {
    ui.horizontal(|ui| {
        ui.add_sized([60.0, 14.0], egui::Label::new(RichText::new(name).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate());
        let w = (ui.available_width() - 44.0).max(20.0);
        let (r, _) = ui.allocate_exact_size(egui::vec2(w, 4.0), Sense::hover());
        ui.painter().rect_filled(r, 2.0, t.hover);
        let mut fill = r;
        fill.set_width(r.width() * f.clamp(0.0, 1.0) as f32);
        let c = if f >= 0.9 { t.danger } else if f >= 0.75 { t.warning } else { t.text_dim };
        ui.painter().rect_filled(fill, 2.0, c);
        ui.label(RichText::new(fmt::pct(f)).font(theme::mono(theme::CAPTION)).color(t.text_dim));
    });
}

impl Quota {
    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        if let Some(tk) = &self.task
            && let Some(r) = tk.take()
        {
            self.loading = false;
            self.task = None;
            if let Err(e) = r {
                self.error = Some(t!("Quota.ReadFailed", e));
            }
        }
        let Some(l) = cx.app.summary.limits.clone() else { return };
        let src = cx.app.summary.usage_source.clone();
        let u = src.as_ref().map(|s| s.usage.clone());
        let has_process = src.as_ref().is_some_and(|s| s.has_process);
        let hint = t!("Quota.Hint", l.seven_day_reset.map(fmt::short_date_time).unwrap_or_default());
        ui.horizontal(|ui| {
            widgets::cap(ui, &t, &t!("Quota.Usage"));
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                widgets::mono_dim(ui, &t, &t!("Quota.Resets", l.five_hour_reset.map(fmt::time).unwrap_or_default()));
            });
        })
        .response
        .on_hover_text(&hint);
        bar_row(ui, &t, &t!("Quota.FiveHours"), l.five_hour);
        bar_row(ui, &t, &t!("Quota.SevenDays"), l.seven_day);
        for m in u.as_ref().map(|u| u.models.clone()).unwrap_or_default() {
            bar_row(ui, &t, &m.name, m.pct);
        }
        if let Some(u) = &u
            && let Some(spent) = u.spent
        {
            let c = match u.spend_limit {
                Some(lim) => format!("{} / {}", u.money(spent), u.money(lim)),
                None => t!("Quota.Used", u.money(spent)),
            };
            ui.horizontal(|ui| {
                widgets::mono_dim(ui, &t, &t!("Quota.Credits"));
                widgets::mono_dim(ui, &t, &c);
            });
        }
        if matches!(l.status.as_str(), "allowed_warning" | "rejected") {
            let rejected = l.status == "rejected";
            let window = i18n::name("Quota.Window.", Some(l.kind.as_deref().unwrap_or("seven_day")));
            let extra = if l.extra_in_use {
                "Quota.ExtraInUse"
            } else if l.extra_on || u.as_ref().and_then(|u| u.extra_on) == Some(true) {
                "Quota.ExtraOn"
            } else {
                "Quota.ExtraOff"
            };
            let text = format!("{} {}", t!(if rejected { "Quota.Blocked" } else { "Quota.Warn" }, window), t!(extra));
            ui.add(egui::Label::new(RichText::new(text).font(egui::FontId::proportional(theme::CAPTION)).color(if rejected { t.danger } else { t.warning })).wrap());
            if ui.link(t!("Quota.Settings")).clicked() {
                crate::backend::open_external("https://claude.ai/settings/usage");
            }
        }
        if has_process {
            let label = format!("{} {}", t!("Quota.Breakdown"), widgets::icon(if self.open { "chev-up" } else { "chev-down" }));
            if ui.add(egui::Button::new(RichText::new(label).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).frame(false)).clicked() {
                self.open = !self.open;
                if self.open
                    && let Some(s) = &src
                    && let Ok(live) = cx.be.session(&s.session_id)
                {
                    self.loading = true;
                    self.error = None;
                    self.task = Some(cx.be.task(async move { live.refresh_usage(true).await }));
                }
            }
        }
        if self.open {
            if let Some(e) = &self.error {
                widgets::error_text(ui, &t, e);
            } else if self.loading {
                widgets::dim(ui, &t, &t!("Quota.Reading"));
            } else if let Some(u) = &u {
                if !u.surfaces.is_empty() {
                    widgets::cap(ui, &t, &t!("Quota.Surfaces"));
                    for r in &u.surfaces {
                        bar_row(ui, &t, &r.name, r.pct);
                    }
                }
                for g in &u.week {
                    widgets::cap(ui, &t, &i18n::name("Quota.Kind.", Some(&g.kind)));
                    let beh = g.kind == "behaviors";
                    for r in &g.rows {
                        let r: UsageRow = if beh { UsageRow { name: i18n::name("Quota.Beh.", Some(&r.name)), ..r.clone() } } else { r.clone() };
                        bar_row(ui, &t, &r.name, r.pct);
                    }
                }
                if u.surfaces.is_empty() && u.week.is_empty() {
                    widgets::dim(ui, &t, &t!("Quota.NoBreakdown"));
                }
            }
        }
        let _ = Color32::TRANSPARENT;
    }
}

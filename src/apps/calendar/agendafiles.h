//
// agendafiles.h -- the two files the Calendar keeps for the agenda applet (src/sys/agenda), beside calendar.ics, as
// Onyx's: agenda.txt (the next month's appointments, "YYYYMMDD|[H:MM ]title" lines in Latin-1) and reminders.txt (the
// applet sends them as notifications: "YYYYMMDDHHMM|text", Latin-1). Included after model.h.
//
#ifndef _calendar_agendafiles_h
#define _calendar_agendafiles_h

// What a reminder says: "Dentiste a 17:30, Dr Peeters".
static void cal_reminder_text (const Event &e, int start, char *t, int cap)
{
	t[0] = '\0';
	scat (t, cap, e.title[0] ? e.title : TR ("(Sans titre)"));
	if (e.allDay) scat (t, cap, start / 1440 > (start - e.remind) / 1440 ? TR (" (demain)") : TR (" (aujourd'hui)"));
	else { char hm[8]; fmt_hm (start, hm, sizeof hm); scat (t, cap, " "); scat (t, cap, TR ("\xC3\xA0")); scat (t, cap, " "); scat (t, cap, hm); }
	if (e.place[0]) { scat (t, cap, ", "); scat (t, cap, e.place); }
}

// agenda.txt and reminders.txt from the events in memory, today being the day number `today`
static void cal_write_agenda (int today)
{
	static Occ occ[400];
	int n = occurrences (today, today + 31, occ, 400, true);
	int cap = n * 120 + 16, len = 0;
	char *b = new char[cap];
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		int dn = occ[i].start / 1440; if (dn < today) dn = today;
		char l[140] = "";
		scatn (l, sizeof l, dn_y (dn)); scat2 (l, sizeof l, dn_m (dn)); scat2 (l, sizeof l, dn_d (dn)); scat (l, sizeof l, "|");
		if (!e.allDay && occ[i].end - occ[i].start < 1440) { char hm[8]; fmt_hm (occ[i].start, hm, sizeof hm); scat (l, sizeof l, hm); scat (l, sizeof l, " "); }
		char ti[100]; to_latin1 (e.title, ti, sizeof ti);
		scat (l, sizeof l, ti); scat (l, sizeof l, "\n");
		for (int k = 0; l[k] && len < cap - 1; k++) b[len++] = l[k];
	}
	save_file (AGENDA_TXT, b, len);
	delete [] b;

	// reminders.txt: the reminders of the next month, "YYYYMMDDHHMM|text" in order -- the agenda applet
	// (always running) sends them as notifications, the calendar open or not.
	static struct R { int at, k; } r[400];
	int nr = 0;
	for (int i = 0; i < n; i++)
	{
		const Event &e = g_ev[occ[i].ev];
		if (e.remind < 0) continue;
		r[nr].at = occ[i].start - e.remind; r[nr].k = i; nr++;
	}
	for (int i = 1; i < nr; i++) { R t = r[i]; int j = i; while (j > 0 && r[j - 1].at > t.at) { r[j] = r[j - 1]; j--; } r[j] = t; }
	cap = nr * 240 + 16; len = 0;
	b = new char[cap];
	for (int i = 0; i < nr; i++)
	{
		const Occ &o = occ[r[i].k]; const Event &e = g_ev[o.ev];
		int at = r[i].at;
		char l[240] = "";
		scatn (l, sizeof l, dn_y (at / 1440)); scat2 (l, sizeof l, dn_m (at / 1440)); scat2 (l, sizeof l, dn_d (at / 1440));
		scat2 (l, sizeof l, at % 1440 / 60); scat2 (l, sizeof l, at % 60); scat (l, sizeof l, "|");
		char rt[200], r1[200]; cal_reminder_text (e, o.start, rt, sizeof rt); to_latin1 (rt, r1, sizeof r1);
		scat (l, sizeof l, r1);
		scat (l, sizeof l, "\n");
		for (int k = 0; l[k] && len < cap - 1; k++) b[len++] = l[k];
	}
	save_file (REMIND_TXT, b, len);
	delete [] b;
}

#endif

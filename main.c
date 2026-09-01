#include <gtk/gtk.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

// Structure pour stocker l'état global de l'application
typedef struct {
    sqlite3 *db;
    GtkWidget *list_box;
    GtkTextBuffer *def_buffer;
    GtkCssProvider *css_provider;
    int font_size;
    int font_index;
    char *current_font;
    int bg_color_index;
    gboolean dark_mode;
    GQueue *history;
} AppState;

// Structure pour la gestion des couleurs du mode jour
typedef struct {
    const char *name;
    const char *hex;
} ColorChoice;

typedef struct {
    GtkLabel *lbl_date;
    GtkLabel *lbl_heure;
} ClockData;


// Fonction portable pour obtenir le dossier de l'exécutable sans <windows.h>
static char *get_app_dir(void) {
    char *dir = NULL;

#if defined(G_OS_WIN32)
    // Récupère la ligne de commande complète en UTF-8 sans API Windows
    gchar **w_argv = g_win32_get_command_line();
    if (w_argv && w_argv[0]) {
        dir = g_path_get_dirname(w_argv[0]);
    }
    g_strfreev(w_argv);
#elif defined(__linux__)
    char *exe_path = g_file_read_link("/proc/self/exe", NULL);
    if (exe_path) {
        dir = g_path_get_dirname(exe_path);
        g_free(exe_path);
    }
#endif

    if (!dir) {
        return g_strdup(".");
    }

    return dir;
}

// Callback pour afficher intro.txt
static void on_intro_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = (AppState *)user_data;

    char *app_dir = get_app_dir();
    char *file_path = g_build_filename(app_dir, "intro.txt", NULL);
    g_free(app_dir);

    char *content = NULL;
    GError *error = NULL;

    if (g_file_get_contents(file_path, &content, NULL, &error)) {
        gtk_text_buffer_set_text(state->def_buffer, content, -1);
        g_free(content);
    } else {
        char *msg = g_strdup_printf("(Impossible d'ouvrir intro.txt dans :\n%s)\nErreur : %s",
                                    file_path, error ? error->message : "inconnue");
        gtk_text_buffer_set_text(state->def_buffer, msg, -1);
        g_free(msg);
        if (error) g_error_free(error);
    }

    g_free(file_path);
}

static const ColorChoice DAY_COLORS[] = {
    {"Grisâtre", "#e6e6e6"},
    {"Vert pâle", "#e2f0d9"},
    {"Bleu pâle", "#ddebf7"},
    {"Rose pâle", "#FFe0ff"},
    {"Jaune pâle", "#fff2cc"}
};
static const int NUM_DAY_COLORS = sizeof(DAY_COLORS) / sizeof(DAY_COLORS[0]);

typedef struct {
    char *text;
    PangoLayout *layout;
    GList *page_breaks;
} PrintData;

typedef struct {
    AppState *state;
    GtkWidget *dialog;
} HistoryData;

// Prototypes
static void display_word_definition(AppState *state, const char *word);
static void add_to_history(AppState *state, const char *word);

static void update_style(AppState *state) {
    char css[512];
    const char *current_font = state->current_font;

    if (state->dark_mode) {
        snprintf(css, sizeof(css),
        "window { background-color: #1e1e1e; color: white; } "
        ".right-panel { background-color: #1e1e1e; } "
        "textview.definition-view { "
        "  font-family: '%s', 'Segoe UI', 'Calibri', 'Arial', sans-serif; "
        "  font-size: %dpx; "
        "  background-color: #252525; "
        "  color: white; "
        "} "
        "list, listrow { background-color: #252525; color: white; } "
        "label { background-color: transparent; } "
        "button { background-color: #303030; color: white; } "
        "entry { background-color: #303030; color: white; } ",
        current_font,
        state->font_size);
    } else {
        const char *bg_hex = DAY_COLORS[state->bg_color_index].hex;

        snprintf(css, sizeof(css),
        "textview.definition-view { "
        "  font-family: '%s', serif; "
        "  font-size: %dpx; "
        "  background-color: %s; "
        "  color: #1a1a1a; "
        "} "
        ".right-panel { background-color: %s; } "
        "label { background-color: transparent; } ",
        current_font,
        state->font_size,
        bg_hex,
        bg_hex);
    }

    gtk_css_provider_load_from_string(state->css_provider, css);
}

static void on_dark_mode_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    state->dark_mode = !state->dark_mode;

    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings,
        "gtk-application-prefer-dark-theme",
        state->dark_mode,
        NULL);

    gtk_button_set_label(
        GTK_BUTTON(button),
        state->dark_mode ? "☀" : "🌙"
    );

    update_style(state);
}

static void on_info_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    GtkWindow *parent = GTK_WINDOW(user_data);

    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "Créé avec IA, 2026, francoistardif43@gmail.com"
    );

    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

static void begin_print(GtkPrintOperation *operation, GtkPrintContext *context, gpointer user_data) {
    PrintData *data = (PrintData *)user_data;
    data->layout = gtk_print_context_create_pango_layout(context);

    PangoFontDescription *desc = pango_font_description_from_string("Noto Serif 12");
    pango_layout_set_font_description(data->layout, desc);
    pango_font_description_free(desc);

    pango_layout_set_text(data->layout, data->text, -1);

    double width = gtk_print_context_get_width(context);
    double height = gtk_print_context_get_height(context);
    pango_layout_set_width(data->layout, (int)(width * PANGO_SCALE));

    int num_lines = pango_layout_get_line_count(data->layout);
    double page_height = 0;
    data->page_breaks = NULL;

    for (int line_idx = 0; line_idx < num_lines; line_idx++) {
        PangoLayoutLine *line = pango_layout_get_line_readonly(data->layout, line_idx);
        PangoRectangle logical_rect;
        pango_layout_line_get_extents(line, NULL, &logical_rect);

        double line_height = ((double)logical_rect.height) / PANGO_SCALE;

        if (page_height + line_height > height) {
            data->page_breaks = g_list_prepend(data->page_breaks, GINT_TO_POINTER(line_idx));
            page_height = 0;
        }
        page_height += line_height;
    }

    data->page_breaks = g_list_reverse(data->page_breaks);
    int num_pages = g_list_length(data->page_breaks) + 1;
    gtk_print_operation_set_n_pages(operation, num_pages);
}

static void draw_page(GtkPrintOperation *operation, GtkPrintContext *context, int page_nr, gpointer user_data) {
    (void)operation;
    PrintData *data = (PrintData *)user_data;
    cairo_t *cr = gtk_print_context_get_cairo_context(context);

    int start_line = 0;
    if (page_nr > 0) {
        GList *link = g_list_nth(data->page_breaks, page_nr - 1);
        start_line = GPOINTER_TO_INT(link->data);
    }

    int end_line;
    if (page_nr < (int)g_list_length(data->page_breaks)) {
        GList *link = g_list_nth(data->page_breaks, page_nr);
        end_line = GPOINTER_TO_INT(link->data);
    } else {
        end_line = pango_layout_get_line_count(data->layout);
    }

    cairo_set_source_rgb(cr, 0, 0, 0);

    PangoLayoutIter *iter = pango_layout_get_iter(data->layout);
    int current_line = 0;
    double start_y = 0;

    do {
        if (current_line == start_line) {
            PangoRectangle logical_rect;
            pango_layout_iter_get_line_extents(iter, NULL, &logical_rect);
            start_y = ((double)logical_rect.y) / PANGO_SCALE;
        }

        if (current_line >= start_line && current_line < end_line) {
            PangoLayoutLine *line = pango_layout_iter_get_line_readonly(iter);
            PangoRectangle logical_rect;
            pango_layout_iter_get_line_extents(iter, NULL, &logical_rect);

            cairo_move_to(cr, ((double)logical_rect.x) / PANGO_SCALE, (((double)logical_rect.y) / PANGO_SCALE) - start_y);
            pango_cairo_show_layout_line(cr, line);
        }

        current_line++;
    } while (current_line < end_line && pango_layout_iter_next_line(iter));

    pango_layout_iter_free(iter);
}

static void end_print(GtkPrintOperation *operation, GtkPrintContext *context, gpointer user_data) {
    (void)operation;
    (void)context;
    PrintData *data = (PrintData *)user_data;
    if (data->layout) g_object_unref(data->layout);
    if (data->page_breaks) g_list_free(data->page_breaks);
    g_free(data->text);
    g_free(data);
}

static void on_print_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = (AppState *)user_data;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(state->def_buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(state->def_buffer, &start, &end, FALSE);

    if (!text || strlen(text) == 0) {
        g_free(text);
        return;
    }

    PrintData *data = g_new0(PrintData, 1);
    data->text = text;

    GtkPrintOperation *print = gtk_print_operation_new();
    gtk_print_operation_set_embed_page_setup(print, TRUE);

    g_signal_connect(print, "begin-print", G_CALLBACK(begin_print), data);
    g_signal_connect(print, "draw-page", G_CALLBACK(draw_page), data);
    g_signal_connect(print, "end-print", G_CALLBACK(end_print), data);

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    GError *error = NULL;
    GtkPrintOperationResult res = gtk_print_operation_run(
        print,
        GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
        GTK_WINDOW(root),
        &error
    );

    if (res == GTK_PRINT_OPERATION_RESULT_ERROR) {
        g_printerr("Erreur lors de l'impression : %s\n", error->message);
        g_error_free(error);
    }

    g_object_unref(print);
}

static void add_to_history(AppState *state, const char *word) {
    if (!word || strlen(word) == 0) return;

    if (!g_queue_is_empty(state->history)) {
        const char *top = (const char *)g_queue_peek_head(state->history);
        if (strcmp(top, word) == 0) return;
    }

    GList *iter = state->history->head;
    while (iter) {
        if (strcmp((const char *)iter->data, word) == 0) {
            char *old_data = (char *)iter->data;
            g_queue_delete_link(state->history, iter);
            g_free(old_data);
            break;
        }
        iter = iter->next;
    }

    g_queue_push_head(state->history, g_strdup(word));

    if (g_queue_get_length(state->history) > 50) {
        char *tail = (char *)g_queue_pop_tail(state->history);
        g_free(tail);
    }
}

int compter_mots_sqlite(sqlite3 *db) {
    sqlite3_stmt *stmt;
    int count = 0;
    const char *sql = "SELECT COUNT(*) FROM edon;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

static void on_history_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    HistoryData *hdata = (HistoryData *)user_data;
    if (!row) return;

    GtkWidget *label = gtk_list_box_row_get_child(row);
    const char *word = gtk_label_get_text(GTK_LABEL(label));

    display_word_definition(hdata->state, word);
    add_to_history(hdata->state, word);

    gtk_window_destroy(GTK_WINDOW(hdata->dialog));
}

static void on_history_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = (AppState *)user_data;

    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Historique des recherches");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 320, 420);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (GTK_IS_WINDOW(root)) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(root));
    }

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *history_list = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), history_list);
    gtk_box_append(GTK_BOX(box), scrolled);

    if (g_queue_is_empty(state->history)) {
        GtkWidget *empty_label = gtk_label_new("(Historique vide)");
        gtk_widget_set_margin_top(empty_label, 20);
        gtk_box_append(GTK_BOX(box), empty_label);
    } else {
        GList *iter = state->history->head;
        while (iter) {
            const char *word = (const char *)iter->data;
            GtkWidget *row = gtk_list_box_row_new();
            GtkWidget *label = gtk_label_new(word);
            gtk_widget_set_halign(label, GTK_ALIGN_START);
            gtk_widget_set_margin_start(label, 10);
            gtk_widget_set_margin_top(label, 6);
            gtk_widget_set_margin_bottom(label, 6);

            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
            gtk_list_box_append(GTK_LIST_BOX(history_list), row);

            iter = iter->next;
        }
    }

    HistoryData *hdata = g_new0(HistoryData, 1);
    hdata->state = state;
    hdata->dialog = dialog;

    g_signal_connect_data(history_list, "row-activated", G_CALLBACK(on_history_row_activated), hdata, (GClosureNotify)g_free, 0);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

    GtkWidget *btn_close = gtk_button_new_with_label("Fermer");
    g_signal_connect_swapped(btn_close, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(btn_box), btn_close);

    gtk_box_append(GTK_BOX(box), btn_box);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_zoom_out_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = (AppState *)user_data;
    if (state->font_size > 10) {
        state->font_size -= 2;
        update_style(state);
    }
}

static void on_zoom_in_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    AppState *state = (AppState *)user_data;
    if (state->font_size < 36) {
        state->font_size += 2;
        update_style(state);
    }
}

static gboolean replace_cb(const GMatchInfo *match_info, GString *result, gpointer user_data) {
    const char *text = user_data;
    gint start_pos;
    g_match_info_fetch_pos(match_info, 0, &start_pos, NULL);

    gint begin = MAX(0, start_pos - 100);
    gchar *ctx = g_strndup(text + begin, start_pos - begin);

    gboolean skip = g_regex_match_simple("\\b[Vv]oy\\.\\s+[^0-9]{1,50}$", ctx, 0, 0);
    g_free(ctx);

    if (!skip)
        g_string_append(result, "\n\n");

    return FALSE;
}

static char *format_definition(const char *raw_def) {
    if (!raw_def)
        return g_strdup("(Aucune définition disponible)");

    GError *error = NULL;
    GRegex *regex = g_regex_new(
        "(?<!voy\\. )(?<!Voy\\. )(?<!V\\. )"
        "(?<!\\(voy\\. )(?<!\\(Voy\\. )(?<!\\(V\\. )"
        "(?<!\\d)(?=\\d+[˚°º])",
        0, 0, &error);

    if (error) {
        g_error_free(error);
        return g_strdup(raw_def);
    }

    char *formatted = g_regex_replace_eval(
        regex, raw_def, -1, 0, 0, replace_cb, (gpointer)raw_def, &error);

    g_regex_unref(regex);

    if (error) {
        g_error_free(error);
        return g_strdup(raw_def);
    }

    char *p = formatted;
    while (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')
        p++;

    char *result = g_strdup(p);
    g_free(formatted);

    return result;
}

static int count_words(const char *start, const char *end) {
    int count = 0;
    int in_word = 0;
    for (const char *p = start; p < end; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            in_word = 0;
        } else {
            if (!in_word) {
                in_word = 1;
                count++;
            }
        }
    }
    return count;
}

static void apply_blue_tags_to_buffer(GtkTextBuffer *buffer, const char *text) {
    GError *error = NULL;
    GRegex *regex = g_regex_new("\\d+[˚°º]", 0, 0, &error);
    if (error) {
        g_error_free(error);
        return;
    }

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *blue_tag = gtk_text_tag_table_lookup(tag_table, "blue_number");
    if (!blue_tag) {
        blue_tag = gtk_text_buffer_create_tag(buffer, "blue_number",
            "foreground", "#0055ff", NULL);
    }

    GMatchInfo *match_info;
    if (g_regex_match(regex, text, 0, &match_info)) {
        while (g_match_info_matches(match_info)) {
            gint start_byte, match_end_byte;
            if (g_match_info_fetch_pos(match_info, 0, &start_byte, &match_end_byte)) {
                gboolean preceded_by_newline = FALSE;
                if (start_byte > 0) {
                    gint p = start_byte - 1;
                    while (p >= 0 && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r')) {
                        p--;
                    }
                    if (p >= 0 && text[p] == '\n') {
                        preceded_by_newline = TRUE;
                    }
                }

                if (preceded_by_newline) {
                    const char *p_curr = text + start_byte;
                    const char *first_dot = NULL;
                    const char *second_dot = NULL;
                    int dot_count = 0;

                    const char *p = p_curr;
                    while (*p != '\0') {
                        if (*p == '.') {
                            dot_count++;
                            if (dot_count == 1) {
                                first_dot = p;
                            } else if (dot_count == 2) {
                                second_dot = p;
                                break;
                            }
                        }
                        p++;
                    }

                    const char *target_end = NULL;
                    if (first_dot) {
                        int words = count_words(p_curr, first_dot);
                        if (words < 3 && second_dot) {
                            target_end = second_dot + 1;
                        } else {
                            target_end = first_dot + 1;
                        }
                    }

                    if (target_end) {
                        gint end_byte = target_end - text;
                        gint start_char = g_utf8_strlen(text, start_byte);
                        gint end_char = g_utf8_strlen(text, end_byte);

                        GtkTextIter start_iter, end_iter;
                        gtk_text_buffer_get_iter_at_offset(buffer, &start_iter, start_char);
                        gtk_text_buffer_get_iter_at_offset(buffer, &end_iter, end_char);

                        gtk_text_buffer_apply_tag(buffer, blue_tag, &start_iter, &end_iter);
                    }
                }
            }
            g_match_info_next(match_info, &error);
        }
    }
    g_match_info_free(match_info);
    g_regex_unref(regex);
}

static void load_words(AppState *state, const char *search_term) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(state->list_box)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(state->list_box), child);
    }

    sqlite3_stmt *stmt;
    char query[2564];

    if (search_term && strlen(search_term) > 0) {
        snprintf(query, sizeof(query), "SELECT mot FROM edon WHERE mot LIKE ? ORDER BY mot LIMIT 1000000;");
        sqlite3_prepare_v2(state->db, query, -1, &stmt, NULL);

        char pattern[2564];
        snprintf(pattern, sizeof(pattern), "%%%s%%", search_term);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    } else {
        snprintf(query, sizeof(query), "SELECT mot FROM edon ORDER BY mot LIMIT 1000000;");
        sqlite3_prepare_v2(state->db, query, -1, &stmt, NULL);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *word = (const char *)sqlite3_column_text(stmt, 0);

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new(word);

        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_top(label, 8);
        gtk_widget_set_margin_bottom(label, 8);

        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(state->list_box), row);
    }

    sqlite3_finalize(stmt);
}

static void display_word_definition(AppState *state, const char *word) {
    if (!word || strlen(word) == 0) {
        gtk_text_buffer_set_text(state->def_buffer, "", -1);
        return;
    }

    sqlite3_stmt *stmt;
    const char *query = "SELECT definition FROM edon WHERE mot = ?;";
    sqlite3_prepare_v2(state->db, query, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, word, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *def = (const char *)sqlite3_column_text(stmt, 0);
        char *formatted_def = format_definition(def);

        gtk_text_buffer_set_text(state->def_buffer, formatted_def, -1);
        apply_blue_tags_to_buffer(state->def_buffer, formatted_def);

        g_free(formatted_def);
    } else {
        gtk_text_buffer_set_text(state->def_buffer, "(Définition introuvable)", -1);
    }

    sqlite3_finalize(stmt);
}

static void on_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    AppState *state = (AppState *)user_data;

    if (!row) {
        gtk_text_buffer_set_text(state->def_buffer, "", -1);
        return;
    }

    GtkWidget *label = gtk_list_box_row_get_child(row);
    const char *word = gtk_label_get_text(GTK_LABEL(label));

    display_word_definition(state, word);
    add_to_history(state, word);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    AppState *state = (AppState *)user_data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    load_words(state, text);
}

static void on_bg_color_clicked(GtkButton *button, gpointer user_data) {
    AppState *state = (AppState *)user_data;

    state->bg_color_index = (state->bg_color_index + 1) % NUM_DAY_COLORS;

    char tooltip[64];
    snprintf(tooltip, sizeof(tooltip), "Fond jour : %s", DAY_COLORS[state->bg_color_index].name);
    gtk_widget_set_tooltip_text(GTK_WIDGET(button), tooltip);

    update_style(state);
}

static void obtenir_date_latin_et_heure(char *buf_date, size_t size_date, char *buf_heure, size_t size_heure) {
    GDateTime *now = g_date_time_new_now_local();
    if (!now) return;

    const char *jours_latin[] = {
        "", "Lunae dies", "Martis dies", "Mercurii dies",
        "Iovis dies", "Veneris dies", "Saturni dies", "Solis dies"
    };

    const char *mois_latin[] = {
        "", "Ianuarii", "Februarii", "Martii", "Aprilis",
        "Maii", "Iunii", "Iulii", "Augusti",
        "Septembris", "Octobris", "Novembris", "Decembris"
    };

    int wday = g_date_time_get_day_of_week(now);
    int day = g_date_time_get_day_of_month(now);
    int month = g_date_time_get_month(now);

    snprintf(buf_date, size_date, "%s %d %s", jours_latin[wday], day, mois_latin[month]);
    snprintf(buf_heure, size_heure, "%02d:%02d:%02d",
             g_date_time_get_hour(now),
             g_date_time_get_minute(now),
             g_date_time_get_second(now));

    g_date_time_unref(now);
}

static gboolean update_clock(gpointer data) {
    ClockData *clock = (ClockData *)data;

    char date_latin[64];
    char heure_str[16];

    obtenir_date_latin_et_heure(date_latin, sizeof(date_latin), heure_str, sizeof(heure_str));

    char buf_date[128];
    snprintf(buf_date, sizeof(buf_date),
             "<span size=\"small\" foreground=\"#888888\">%s</span>",
             date_latin);

    char buf_heure[64];
    snprintf(buf_heure, sizeof(buf_heure),
             "<span size=\"small\" foreground=\"#888888\">%s</span>",
             heure_str);

    gtk_label_set_markup(clock->lbl_date, buf_date);
    gtk_label_set_markup(clock->lbl_heure, buf_heure);

    return G_SOURCE_CONTINUE;
}

static void on_font_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AppState *state = user_data;

    guint index = gtk_drop_down_get_selected(dropdown);
    GtkStringObject *obj = GTK_STRING_OBJECT(
        g_list_model_get_item(gtk_drop_down_get_model(dropdown), index));

    const char *font = gtk_string_object_get_string(obj);

    g_free(state->current_font);
    state->current_font = g_strdup(font);

    g_object_unref(obj);
    update_style(state);
}

static void activate(GtkApplication *app, gpointer user_data) {
    AppState *state = (AppState *)user_data;

    state->font_size = 14;
    state->current_font = g_strdup("Serif");
    state->bg_color_index = 0;
    state->dark_mode = FALSE;

    state->css_provider = gtk_css_provider_new();
    update_style(state);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(state->css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "dictionnaire Édon");
    gtk_window_set_default_size(GTK_WINDOW(window), 970, 550);

    // --- CONTENEUR PRINCIPAL ---
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), main_vbox);

    // --- PANNEAU SPLIT ---
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 300);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(main_vbox), paned);

    // --- PANNEAU GAUCHE ---
    GtkWidget *left_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(left_box, 8);
    gtk_widget_set_margin_end(left_box, 8);
    gtk_widget_set_margin_top(left_box, 8);
    gtk_widget_set_margin_bottom(left_box, 8);

    GtkWidget *search_entry = gtk_search_entry_new();
    gtk_widget_set_margin_bottom(search_entry, 6);
    gtk_box_append(GTK_BOX(left_box), search_entry);

    GtkWidget *scrolled_left = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_left, TRUE);

    state->list_box = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_left), state->list_box);
    gtk_box_append(GTK_BOX(left_box), scrolled_left);

    gtk_paned_set_start_child(GTK_PANED(paned), left_box);

    // --- PANNEAU DROIT ---
    GtkWidget *right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(right_box, "right-panel");
    gtk_widget_set_margin_start(right_box, 8);
    gtk_widget_set_margin_end(right_box, 8);
    gtk_widget_set_margin_top(right_box, 8);
    gtk_widget_set_margin_bottom(right_box, 8);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *lbl_title = gtk_label_new("Définition");
    gtk_widget_set_halign(lbl_title, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lbl_title, TRUE);

    GtkWidget *btn_intro = gtk_button_new();
    GtkWidget *lbl_intro_e = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_intro_e), "<span foreground=\"red\" weight=\"bold\">E</span>");
    gtk_button_set_child(GTK_BUTTON(btn_intro), lbl_intro_e);
    gtk_widget_set_tooltip_text(btn_intro, "Afficher l'introduction (intro.txt)");
    g_signal_connect(btn_intro, "clicked", G_CALLBACK(on_intro_clicked), state);

    GtkWidget *btn_color = gtk_button_new_with_label("🎨");
    gtk_widget_set_tooltip_text(btn_color, "Fond jour : Grisâtre");

    GtkStringList *font_list = gtk_string_list_new(NULL);
    PangoFontMap *fontmap = pango_cairo_font_map_get_default();
    PangoFontFamily **families;
    int n_families;

    pango_font_map_list_families(fontmap, &families, &n_families);
    for (int i = 0; i < n_families; i++) {
        gtk_string_list_append(font_list, pango_font_family_get_name(families[i]));
    }

    GtkWidget *font_dropdown = gtk_drop_down_new(G_LIST_MODEL(font_list), NULL);
    for (int i = 0; i < n_families; i++) {
        const char *name = pango_font_family_get_name(families[i]);
        if (g_strcmp0(name, "Times New Roman") == 0 || g_strcmp0(name, "Serif") == 0) {
            gtk_drop_down_set_selected(GTK_DROP_DOWN(font_dropdown), i);
            break;
        }
    }
    g_free(families);

    GtkWidget *btn_zoom_out = gtk_button_new_with_label("A-");
    GtkWidget *btn_zoom_in = gtk_button_new_with_label("A+");
    gtk_widget_set_tooltip_text(btn_zoom_out, "Réduire la taille du texte");
    gtk_widget_set_tooltip_text(btn_zoom_in, "Agrandir la taille du texte");

    GtkWidget *btn_print = gtk_button_new_from_icon_name("document-print-symbolic");
    gtk_widget_set_tooltip_text(btn_print, "Imprimer la définition");

    GtkWidget *btn_history = gtk_button_new_from_icon_name("document-open-recent-symbolic");
    gtk_widget_set_tooltip_text(btn_history, "Historique des recherches");

    GtkWidget *btn_info = gtk_button_new_from_icon_name("dialog-information-symbolic");
    gtk_widget_set_tooltip_text(btn_info, "Informations");

    GtkWidget *btn_dark = gtk_button_new_with_label("🌙");
    gtk_widget_set_tooltip_text(btn_dark, "Mode sombre");

    gtk_box_append(GTK_BOX(toolbar), lbl_title);
    gtk_box_append(GTK_BOX(toolbar), btn_intro);
    gtk_box_append(GTK_BOX(toolbar), btn_color);
    gtk_box_append(GTK_BOX(toolbar), font_dropdown);
    gtk_box_append(GTK_BOX(toolbar), btn_dark);
    gtk_box_append(GTK_BOX(toolbar), btn_zoom_out);
    gtk_box_append(GTK_BOX(toolbar), btn_zoom_in);
    gtk_box_append(GTK_BOX(toolbar), btn_print);
    gtk_box_append(GTK_BOX(toolbar), btn_history);
    gtk_box_append(GTK_BOX(toolbar), btn_info);
    gtk_box_append(GTK_BOX(right_box), toolbar);

    GtkWidget *scrolled_right = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scrolled_right, TRUE);
    gtk_widget_set_vexpand(scrolled_right, TRUE);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_widget_add_css_class(text_view, "definition-view");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);

    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 15);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 15);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 15);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 15);

    state->def_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_right), text_view);

    gtk_box_append(GTK_BOX(right_box), scrolled_right);
    gtk_paned_set_end_child(GTK_PANED(paned), right_box);

    // --- BARRE DE STATUT ---
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_start(status_box, 12);
    gtk_widget_set_margin_end(status_box, 12);
    gtk_widget_set_margin_top(status_box, 4);
    gtk_widget_set_margin_bottom(status_box, 4);

    int nb_mots = compter_mots_sqlite(state->db);
    char buf_mots[128];
    if (nb_mots > 0) {
        snprintf(buf_mots, sizeof(buf_mots),
                 "<span size=\"small\" foreground=\"#888888\">%d verba recte onerata 😁</span>",
                 nb_mots);
    } else {
        snprintf(buf_mots, sizeof(buf_mots),
                 "<span size=\"small\" foreground=\"#888888\">%d verba onerata!!! 🤔</span>",
                 nb_mots);
    }

    GtkWidget *lbl_mots = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_mots), buf_mots);
    gtk_widget_set_halign(lbl_mots, GTK_ALIGN_START);
    gtk_widget_set_hexpand(lbl_mots, TRUE);

    char date_latin[64];
    char heure_str[32];
    obtenir_date_latin_et_heure(date_latin, sizeof(date_latin), heure_str, sizeof(heure_str));

    char buf_date[128];
    snprintf(buf_date, sizeof(buf_date),
             "<span size=\"small\" foreground=\"#888888\">%s</span>",
             date_latin);

    GtkWidget *lbl_date = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_date), buf_date);
    gtk_widget_set_halign(lbl_date, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(lbl_date, TRUE);

    char buf_heure[128];
    snprintf(buf_heure, sizeof(buf_heure),
             "<span size=\"small\" foreground=\"#888888\">%s</span>",
             heure_str);

    GtkWidget *lbl_heure = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_heure), buf_heure);
    gtk_widget_set_halign(lbl_heure, GTK_ALIGN_END);
    gtk_widget_set_hexpand(lbl_heure, TRUE);

    ClockData *clock = g_new(ClockData, 1);
    clock->lbl_date = GTK_LABEL(lbl_date);
    clock->lbl_heure = GTK_LABEL(lbl_heure);

    update_clock(clock);
    g_timeout_add_seconds(1, update_clock, clock);

    gtk_box_append(GTK_BOX(status_box), lbl_mots);
    gtk_box_append(GTK_BOX(status_box), lbl_date);
    gtk_box_append(GTK_BOX(status_box), lbl_heure);
    gtk_box_append(GTK_BOX(main_vbox), status_box);

    // --- SIGNAUX ---
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), state);
    g_signal_connect(state->list_box, "row-selected", G_CALLBACK(on_row_selected), state);
    g_signal_connect(btn_color, "clicked", G_CALLBACK(on_bg_color_clicked), state);
    g_signal_connect(font_dropdown, "notify::selected", G_CALLBACK(on_font_changed), state);
    g_signal_connect(btn_zoom_out, "clicked", G_CALLBACK(on_zoom_out_clicked), state);
    g_signal_connect(btn_zoom_in, "clicked", G_CALLBACK(on_zoom_in_clicked), state);
    g_signal_connect(btn_print, "clicked", G_CALLBACK(on_print_clicked), state);
    g_signal_connect(btn_history, "clicked", G_CALLBACK(on_history_clicked), state);
    g_signal_connect(btn_info, "clicked", G_CALLBACK(on_info_clicked), window);
    g_signal_connect(btn_dark, "clicked", G_CALLBACK(on_dark_mode_clicked), state);

    load_words(state, NULL);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    AppState state;
    state.history = g_queue_new();

    char *app_dir = get_app_dir();
    char *db_path = g_build_filename(app_dir, "edon.db", NULL);
    g_free(app_dir);

    if (sqlite3_open(db_path, &state.db) != SQLITE_OK) {
        g_printerr("Erreur : Impossible d'ouvrir %s : %s\n",
                   db_path, sqlite3_errmsg(state.db));
        g_free(db_path);
        g_queue_free(state.history);
        return 1;
    }
    g_free(db_path);

    const char *sql_create = "CREATE TABLE IF NOT EXISTS edon (mot TEXT PRIMARY KEY, definition TEXT);";
    char *err_msg = NULL;
    sqlite3_exec(state.db, sql_create, 0, 0, &err_msg);
    if (err_msg) sqlite3_free(err_msg);

    GtkApplication *app = gtk_application_new("com.edon.dictionary", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &state);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    sqlite3_close(state.db);
    g_queue_free_full(state.history, g_free);

    return status;
}

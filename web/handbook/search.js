// The search of the handbook, as QcHandbook::search: every word of the query in the titles (this
// language and English) or the name, else in the texts; best matches first, at most 50. Without the
// page around it, so test_web_handbook runs it in QJSEngine against the C++ search.
"use strict";

// QcHandbook::defaultSearchLimit.
const handbookSearchLimit = 50;

// QcHandbook::searchKey: decomposed, without accents (the combining-mark blocks), lower case. Like
// QString::toCaseFolded (one character to one), ß stays ß; test_web_handbook compares both.
function handbookSearchKey(text) {
    return text.normalize("NFKD").replace(/[\u0300-\u036f\u1ab0-\u1aff\u1dc0-\u1dff\u20d0-\u20ff\ufe20-\ufe2f]/g, "").toLowerCase();
}

// Adds the search keys to the entries of a book (data/<language>.json) once.
function prepareSearch(book) {
    book.categoryOrder = new Map(book.categories.map((category, index) => [category.id, index]));
    for (const entry of Object.values(book.entries)) {
        entry.titleKey = handbookSearchKey(entry.title) + "\n" + handbookSearchKey(entry.englishTitle) + "\n" + handbookSearchKey(entry.name);
        let text = entry.text;
        for (const section of entry.sections)
            text += "\n" + section.title + "\n" + section.text;
        entry.textKey = handbookSearchKey(text);
    }
}

// QcHandbook::matchRank: lower is better, undefined when a word is missing.
function handbookMatchRank(entry, query, words) {
    let inTitles = true;
    for (const word of words) {
        if (entry.titleKey.includes(word))
            continue;
        inTitles = false;
        if (!entry.textKey.includes(word))
            return undefined;
    }
    if (!inTitles)
        return 3;

    const [title, englishTitle] = entry.titleKey.split("\n");
    if (title === query)
        return -1;
    if (title.startsWith(query))
        return 0;
    if (title.includes(" " + words[0]) || englishTitle.startsWith(query))
        return 1;
    return 2;
}

// The entries of a prepared book that match `text`, best first.
function searchHandbook(book, text) {
    const query = handbookSearchKey(text).trim().replace(/\s+/g, " ");
    if (query.length === 0)
        return [];
    const words = query.split(" ");
    const matches = [];
    for (const entry of Object.values(book.entries)) {
        const rank = handbookMatchRank(entry, query, words);
        if (rank !== undefined) {
            const category = book.categoryOrder.has(entry.category) ? book.categoryOrder.get(entry.category) : book.categories.length;
            matches.push({ rank: rank, category: category, entry: entry });
        }
    }
    matches.sort((a, b) => a.rank - b.rank || a.category - b.category || (a.entry.titleKey < b.entry.titleKey ? -1 : a.entry.titleKey > b.entry.titleKey ? 1 : 0));
    return matches.slice(0, handbookSearchLimit).map(match => match.entry);
}

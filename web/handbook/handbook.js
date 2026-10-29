// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The handbook screen of the game (HandbookScreen.qml) as a web page: categories on the left, the
// entries of a category (or the search results) in the middle and the open page on the right.
// Links and recipe cells open other pages; the address (#<language>/<entry id>) can be shared and
// the browser's Back returns. Data: data/<language>.json (QcHandbookWeb), data/icons.json and
// data/icons.png (QcWebHandbookAssets); the search is in search.js.
"use strict";

// Texts of the handbook screen, from translations/qtcraft_*.ts.
const labels = {
    en: {
        back: "‹ Back",
        handbook: "Handbook",
        search: "Search blocks, items, creatures …",
        nothing: "Nothing found.",
        hint: "Click a name or an item to open its page. Backspace goes back.",
        language: "Language",
        contents: "Contents",
    },
    de: {
        back: "‹ Zurück",
        handbook: "Handbuch",
        search: "Blöcke, Items, Wesen suchen …",
        nothing: "Nichts gefunden.",
        hint: "Klicke auf einen Namen oder ein Item, um seine Seite zu öffnen. Rücktaste geht zurück.",
        language: "Sprache",
        contents: "Inhalt",
    },
};

const view = {
    screen: document.getElementById("screen"),
    back: document.getElementById("back"),
    contents: document.getElementById("contents"),
    heading: document.getElementById("heading"),
    search: document.getElementById("search"),
    language: document.getElementById("language"),
    categories: document.getElementById("categories"),
    entries: document.getElementById("entries"),
    page: document.getElementById("page"),
    hint: document.getElementById("hint"),
};

const state = {
    icons: null,
    languages: [],
    books: new Map(),
    book: null,
    language: "en",
    category: "",
    current: "",
    // How many pages of this visit lie before the shown one (kept in history.state, so the
    // browser's own Back and Forward keep it right); Back is enabled while it is above 0.
    depth: 0,
    // Counts route() calls; a slower earlier one (loading a language) does not overwrite a later one.
    routes: 0,
};

function element(tag, className, text) {
    const node = document.createElement(tag);
    if (className)
        node.className = className;
    if (text !== undefined)
        node.textContent = text;
    return node;
}

function label(key) {
    return (labels[state.language] || labels.en)[key];
}

async function fetchJson(file) {
    const response = await fetch("data/" + file);
    if (!response.ok)
        throw new Error(file + ": " + response.status);
    return response.json();
}

// An item icon of `size` pixels from the icon sheet; an empty box when the item has none.
function icon(name, size) {
    const node = element("span", "icon");
    node.style.width = node.style.height = size + "px";
    const cell = state.icons.icons[name];
    if (cell === undefined)
        return node;
    const scale = size / state.icons.size;
    const columns = state.icons.columns;
    const rows = Math.ceil(Object.keys(state.icons.icons).length / columns);
    node.style.backgroundSize = columns * size + "px " + rows * size + "px";
    node.style.backgroundPosition = -(cell % columns) * state.icons.size * scale + "px " + -Math.floor(cell / columns) * state.icons.size * scale + "px";
    return node;
}

// --- Texts: the Markdown subset of the handbook (paragraphs, "- " lists, **bold**, `code`).

function inline(text, parent) {
    for (const part of text.split(/(\*\*[^*]+\*\*|`[^`]+`)/)) {
        if (part.startsWith("**") && part.endsWith("**") && part.length > 4)
            parent.append(element("strong", "", part.slice(2, -2)));
        else if (part.startsWith("`") && part.endsWith("`") && part.length > 2)
            parent.append(element("code", "", part.slice(1, -1)));
        else if (part.length > 0)
            parent.append(part);
    }
}

function markdown(text) {
    const node = element("div", "text");
    for (const block of text.split(/\n\s*\n/)) {
        let paragraph = null;
        let list = null;
        for (const line of block.split("\n")) {
            if (line.startsWith("- ")) {
                paragraph = null;
                if (!list)
                    list = node.appendChild(element("ul"));
                inline(line.slice(2), list.appendChild(element("li")));
            } else if (line.trim().length > 0) {
                list = null;
                if (paragraph)
                    paragraph.append(" ");
                else
                    paragraph = node.appendChild(element("p"));
                inline(line, paragraph);
            }
        }
    }
    return node;
}

// --- The page.

function href(target, language = state.language) {
    return "#" + language + "/" + encodeURIComponent(target);
}

function chip(link) {
    const node = element("a", "chip");
    node.href = href(link.target);
    if (link.icon)
        node.append(icon(link.icon, 20));
    node.append(element("span", "", link.text));
    if (link.note)
        node.append(element("span", "note", "(" + link.note + ")"));
    return node;
}

// Empty links (null in the data) show nothing.
function chips(links, className) {
    const node = element("div", "flow" + (className ? " " + className : ""));
    for (const link of links) {
        if (link)
            node.append(chip(link));
    }
    return node;
}

function cell(link, count) {
    const node = element(link ? "a" : "span", "cell");
    if (!link)
        return node;
    node.href = href(link.target);
    node.title = link.text;
    if (link.icon)
        node.append(icon(link.icon, 32));
    else
        node.append(link.text);
    if (count > 1)
        node.append(element("span", "amount", String(count)));
    return node;
}

function recipe(data) {
    const node = element("div", "recipe");
    const row = node.appendChild(element("div", "recipe-row"));
    const grid = row.appendChild(element("div", "grid"));
    grid.style.gridTemplateColumns = "repeat(" + data.width + ", 44px)";
    for (const link of data.cells)
        grid.append(cell(link, 1));
    row.append(element("span", data.kind === "machine" ? "arrow machine" : "arrow", data.kind === "machine" ? "⟶" : "→"));
    row.append(cell(data.result, data.count));
    if (data.note)
        node.append(element("div", "note", data.note));
    return node;
}

function facts(list) {
    const node = element("div", "facts");
    for (const fact of list) {
        node.append(element("div", "label", fact.label));
        const value = node.appendChild(element("div", "flow"));
        if (fact.value)
            value.append(element("span", "value", fact.value));
        value.append(...chips(fact.links).childNodes);
    }
    return node;
}

function section(data) {
    const node = element("section", "section");
    if (data.title)
        node.append(element("h3", "", data.title));
    if (data.text)
        node.append(markdown(data.text));
    if (data.recipes.length > 0) {
        const recipes = node.appendChild(element("div", "flow recipes"));
        for (const item of data.recipes)
            recipes.append(recipe(item));
    }
    if (data.links.length > 0)
        node.append(chips(data.links));
    return node;
}

function showPage(entry) {
    const page = view.page;
    page.replaceChildren();
    const head = page.appendChild(element("div", "page-head"));
    if (entry.icon)
        head.appendChild(element("div", "page-icon")).append(icon(entry.icon, 44));
    const titles = head.appendChild(element("div"));
    titles.append(element("h2", "", entry.title), element("div", "meta", entry.categoryTitle + "  ·  " + entry.packName));
    if (entry.text)
        page.append(markdown(entry.text));
    if (entry.facts.length > 0)
        page.append(facts(entry.facts));
    for (const data of entry.sections)
        page.append(section(data));
    page.scrollTop = 0;
    document.title = entry.title + " – QtCraft " + label("handbook");
}

// --- Lists.

function showCategories() {
    view.categories.replaceChildren();
    for (const category of state.book.categories) {
        const row = element("button", "row");
        row.type = "button";
        row.classList.toggle("selected", !searching() && category.id === state.category);
        row.append(element("span", "", category.title), element("span", "count", String(category.entries.length)));
        row.addEventListener("click", () => {
            view.search.value = "";
            state.category = category.id;
            render();
        });
        view.categories.append(row);
    }
}

function searching() {
    return view.search.value.trim().length > 0;
}

// Rows of the middle list: the search results, or the category with a heading per pack when more
// than the base game is documented.
function listed() {
    const book = state.book;
    if (searching())
        return searchHandbook(book, view.search.value);
    const category = book.categories.find(item => item.id === state.category);
    if (!category)
        return [];
    const rows = [];
    let pack = null;
    for (const id of category.entries) {
        const entry = book.entries[id];
        if (book.packs.length > 1 && entry.pack !== pack) {
            pack = entry.pack;
            rows.push({ header: entry.packName });
        }
        rows.push(entry);
    }
    return rows;
}

function showEntries() {
    const rows = listed();
    view.entries.replaceChildren();
    if (rows.length === 0 && searching())
        view.entries.append(element("div", "empty", label("nothing")));
    let selected = null;
    for (const row of rows) {
        if (row.header !== undefined) {
            view.entries.append(element("div", "header", row.header));
            continue;
        }
        const node = element("a", "row");
        node.href = href(row.id);
        node.setAttribute("role", "option");
        if (row.icon)
            node.append(icon(row.icon, 24));
        const text = node.appendChild(element("span", "label"));
        text.append(element("span", "title", row.title));
        if (searching())
            text.append(element("span", "sub", row.categoryTitle));
        if (row.id === state.current) {
            node.classList.add("selected");
            selected = node;
        }
        view.entries.append(node);
    }
    if (selected)
        selected.scrollIntoView({ block: "nearest" });
}

function render() {
    showCategories();
    showEntries();
}

// --- Navigation.

function firstEntry(book) {
    const playing = book.categories.find(category => category.id === "playing") || book.categories[0];
    return playing ? playing.entries[0] : "";
}

async function loadLanguage(language) {
    if (!state.books.has(language)) {
        const book = await fetchJson(language + ".json");
        prepareSearch(book);
        state.books.set(language, book);
    }
    state.language = language;
    state.book = state.books.get(language);
    document.documentElement.lang = language;
    view.back.textContent = label("back");
    view.heading.textContent = label("handbook");
    view.search.placeholder = label("search");
    view.language.setAttribute("aria-label", label("language"));
    view.contents.title = label("contents");
    view.hint.textContent = label("hint");
    view.language.value = language;
}

// Shows the entry the address names; an unknown or missing entry opens the first chapter.
async function route() {
    const routed = ++state.routes;
    const [language, ...rest] = location.hash.slice(1).split("/");
    const known = state.languages.some(item => item.id === language);
    await loadLanguage(known ? language : state.language);
    if (routed !== state.routes)
        return;

    let id = "";
    try {
        id = decodeURIComponent(rest.join("/"));
    } catch {
        id = "";
    }
    if (!state.book.entries[id]) {
        id = firstEntry(state.book);
        history.replaceState({ depth: state.depth }, "", href(id));
    }
    const entry = state.book.entries[id];
    state.current = id;
    if (!searching())
        state.category = entry.category;
    showPage(entry);
    render();
}

// A language that cannot be loaded (offline, server gone) keeps the shown page and language and
// says so above the page.
function showLoadError(error) {
    view.language.value = state.language;
    if (state.current)
        history.replaceState(history.state, "", href(state.current));
    const note = element("p", "load-error", "The handbook data could not be loaded: " + error.message);
    view.page.querySelector(".load-error")?.remove();
    view.page.prepend(note);
}

function preferredLanguage() {
    for (const wanted of navigator.languages || [navigator.language]) {
        const id = String(wanted).slice(0, 2).toLowerCase();
        if (state.languages.some(item => item.id === id))
            return id;
    }
    return "en";
}

function connect() {
    // A new page has no history.state yet: one deeper than the page it was opened from. Back and
    // Forward of the browser bring the depth stored with that page.
    window.addEventListener("hashchange", () => {
        if (history.state && Number.isInteger(history.state.depth)) {
            state.depth = history.state.depth;
        } else {
            state.depth += 1;
            history.replaceState({ depth: state.depth }, "", location.hash);
        }
        view.back.disabled = state.depth === 0;
        view.screen.classList.add("show-page");
        route().catch(showLoadError);
    });
    view.back.addEventListener("click", () => {
        if (state.depth > 0)
            history.back();
    });
    view.contents.addEventListener("click", () => view.screen.classList.remove("show-page"));
    view.search.addEventListener("input", () => {
        view.screen.classList.remove("show-page");
        render();
    });
    view.search.addEventListener("keydown", event => {
        if (event.key === "Enter") {
            const first = listed().find(row => row.id !== undefined);
            if (first)
                location.hash = href(first.id);
        } else if (event.key === "Escape") {
            view.search.value = "";
            render();
        } else if (event.key === "ArrowDown") {
            const first = view.entries.querySelector("a");
            if (first)
                first.focus();
            event.preventDefault();
        }
    });
    view.entries.addEventListener("keydown", event => {
        const rows = [...view.entries.querySelectorAll("a")];
        const index = rows.indexOf(document.activeElement);
        const next = event.key === "ArrowDown" ? index + 1 : event.key === "ArrowUp" ? index - 1 : -2;
        if (next >= 0 && next < rows.length) {
            rows[next].focus();
            event.preventDefault();
        }
    });
    view.language.addEventListener("change", () => {
        location.hash = href(state.current, view.language.value);
    });
    document.addEventListener("keydown", event => {
        const typing = event.target === view.search || event.target === view.language;
        if (event.key === "Backspace" && !typing && !view.back.disabled)
            view.back.click();
    });
}

async function start() {
    try {
        const [languages, icons] = await Promise.all([fetchJson("languages.json"), fetchJson("icons.json")]);
        state.languages = languages.languages;
        state.icons = icons;
        for (const language of state.languages)
            view.language.append(new Option(language.name, language.id));
        state.language = preferredLanguage();
        // A shared address opens its page at once, also on a phone.
        const deepLink = location.hash.includes("/");
        history.replaceState({ depth: 0 }, "", location.hash);
        connect();
        await route();
        view.screen.classList.toggle("show-page", deepLink);
    } catch (error) {
        view.page.replaceChildren(element("p", "", "The handbook data could not be loaded: " + error.message));
    }
}

start();

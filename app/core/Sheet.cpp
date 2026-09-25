#include "core/Sheet.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

#include "core/Zip.h"

namespace {

constexpr char kXmlHead[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
constexpr char kOdsType[] = "application/vnd.oasis.opendocument.spreadsheet";

// Text made safe inside an element or an attribute; the control characters
// XML forbids are dropped.
std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char letter : text) {
        unsigned char byte = static_cast<unsigned char>(letter);
        switch (letter) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            if (byte >= 0x20 || letter == '\t' || letter == '\n' || letter == '\r') {
                out += letter;
            }
        }
    }
    return out;
}

// How many characters a UTF-8 text shows, to size a column.
size_t Shown(const std::string& text) {
    return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char letter) {
        return (static_cast<unsigned char>(letter) & 0xC0) != 0x80;
    }));
}

// The width of each column, in characters, from its longest cell.
std::vector<size_t> Widths(const sheet::Rows& rows) {
    std::vector<size_t> widths;
    for (const std::vector<sheet::Cell>& row : rows) {
        if (widths.size() < row.size()) {
            widths.resize(row.size(), 0);
        }
        for (size_t column = 0; column < row.size(); ++column) {
            widths[column] = std::max(widths[column], Shown(row[column].text));
        }
    }
    for (size_t& width : widths) {
        width = std::clamp<size_t>(width + 2, 8, 80);
    }
    return widths;
}

// "A", "B", … "Z", "AA": the letters of a column in a cell reference.
std::string ColumnName(size_t column) {
    std::string name;
    ++column;
    while (column > 0) {
        --column;
        name.insert(name.begin(), static_cast<char>('A' + column % 26));
        column /= 26;
    }
    return name;
}

// --- reading XML --------------------------------------------------------

// One tag of a document: its name, its attributes, and whether it opens,
// closes, or does both.
struct Tag {
    std::string name;
    std::map<std::string, std::string> attributes;
    bool closing = false;
    bool empty = false;
};

// Turns the entities of XML back into the characters they stand for.
std::string Unescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size(); ++at) {
        if (text[at] != '&') {
            out += text[at];
            continue;
        }
        size_t end = text.find(';', at);
        if (end == std::string::npos) {
            out += text[at];
            continue;
        }
        std::string name = text.substr(at + 1, end - at - 1);
        unsigned long code = 0;
        if (name == "amp") {
            code = '&';
        } else if (name == "lt") {
            code = '<';
        } else if (name == "gt") {
            code = '>';
        } else if (name == "quot") {
            code = '"';
        } else if (name == "apos") {
            code = '\'';
        } else if (name.size() > 1 && name[0] == '#') {
            code = name[1] == 'x' || name[1] == 'X' ? std::strtoul(name.c_str() + 2, nullptr, 16)
                                                    : std::strtoul(name.c_str() + 1, nullptr, 10);
        }
        if (code == 0) {
            out += text[at];
            continue;
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        at = end;
    }
    return out;
}

// Walks a document tag by tag, handing over the text met before each tag.
// Declarations, comments and processing instructions are passed over.
class XmlReader {
public:
    explicit XmlReader(std::string text) : text_(std::move(text)) {}

    bool Next(Tag* tag, std::string* before) {
        before->clear();
        while (pos_ < text_.size()) {
            size_t open = text_.find('<', pos_);
            if (open == std::string::npos) {
                *before += Unescape(text_.substr(pos_));
                pos_ = text_.size();
                return false;
            }
            *before += Unescape(text_.substr(pos_, open - pos_));
            if (text_.compare(open, 9, "<![CDATA[") == 0) {
                size_t close = text_.find("]]>", open);
                close = close == std::string::npos ? text_.size() : close;
                *before += text_.substr(open + 9, close - open - 9);
                pos_ = std::min(text_.size(), close + 3);
                continue;
            }
            if (text_.compare(open, 4, "<!--") == 0) {
                size_t close = text_.find("-->", open);
                pos_ = close == std::string::npos ? text_.size() : close + 3;
                continue;
            }
            size_t close = text_.find('>', open);
            if (close == std::string::npos) {
                pos_ = text_.size();
                return false;
            }
            pos_ = close + 1;
            if (text_[open + 1] == '?' || text_[open + 1] == '!') {
                continue;
            }
            Parse(text_.substr(open + 1, close - open - 1), tag);
            return true;
        }
        return false;
    }

private:
    static void Parse(std::string body, Tag* tag) {
        *tag = Tag();
        if (!body.empty() && body.back() == '/') {
            tag->empty = true;
            body.pop_back();
        }
        if (!body.empty() && body.front() == '/') {
            tag->closing = true;
            body.erase(0, 1);
        }
        size_t at = body.find_first_of(" \t\r\n");
        tag->name = body.substr(0, at);
        while (at != std::string::npos && at < body.size()) {
            size_t start = body.find_first_not_of(" \t\r\n", at);
            if (start == std::string::npos) {
                break;
            }
            size_t equals = body.find('=', start);
            if (equals == std::string::npos) {
                break;
            }
            std::string key = body.substr(start, equals - start);
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            size_t quote = body.find_first_of("\"'", equals);
            if (quote == std::string::npos) {
                break;
            }
            size_t end = body.find(body[quote], quote + 1);
            if (end == std::string::npos) {
                break;
            }
            tag->attributes[key] = Unescape(body.substr(quote + 1, end - quote - 1));
            at = end + 1;
        }
    }

    std::string text_;
    size_t pos_ = 0;
};

std::string Attribute(const Tag& tag, const std::string& name) {
    auto found = tag.attributes.find(name);
    return found == tag.attributes.end() ? std::string() : found->second;
}

// Puts a value at a column of a row, the row growing to reach it.
void Place(std::vector<std::string>& row, size_t column, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (row.size() <= column) {
        row.resize(column + 1);
    }
    row[column] = value;
}

// --- Excel --------------------------------------------------------------

// The strings a workbook shares between its cells, in their order.
std::vector<std::string> SharedStrings(const std::string& xml) {
    std::vector<std::string> strings;
    XmlReader reader(xml);
    Tag tag;
    std::string text;
    std::string current;
    bool inText = false;
    int phonetic = 0;
    while (reader.Next(&tag, &text)) {
        if (inText && phonetic == 0) {
            current += text;
        }
        if (tag.name == "si") {
            if (tag.closing) {
                strings.push_back(current);
            } else {
                current.clear();
                if (tag.empty) {
                    strings.emplace_back();
                }
            }
        } else if (tag.name == "t") {
            inText = !tag.closing && !tag.empty;
        } else if (tag.name == "rPh") {
            phonetic += tag.closing ? -1 : (tag.empty ? 0 : 1);
        }
    }
    return strings;
}

// The column of a cell from its reference: "C12" is column 2.
size_t ColumnOf(const std::string& reference, size_t fallback) {
    size_t column = 0;
    size_t letters = 0;
    for (char letter : reference) {
        if (letter < 'A' || letter > 'Z') {
            break;
        }
        column = column * 26 + static_cast<size_t>(letter - 'A' + 1);
        ++letters;
    }
    return letters == 0 ? fallback : column - 1;
}

// The path of the first sheet, from the workbook and its relations.
std::string FirstSheetPath(const std::vector<zip::Entry>& entries) {
    std::string id;
    XmlReader workbook(zip::Find(entries, "xl/workbook.xml"));
    Tag tag;
    std::string text;
    while (workbook.Next(&tag, &text)) {
        if (tag.name == "sheet" && !tag.closing) {
            id = Attribute(tag, "r:id");
            break;
        }
    }
    XmlReader relations(zip::Find(entries, "xl/_rels/workbook.xml.rels"));
    while (!id.empty() && relations.Next(&tag, &text)) {
        if (tag.name == "Relationship" && Attribute(tag, "Id") == id) {
            std::string target = Attribute(tag, "Target");
            return target.rfind('/', 0) == 0 ? target.substr(1) : "xl/" + target;
        }
    }
    return "xl/worksheets/sheet1.xml";
}

std::vector<std::vector<std::string>> ReadXlsx(const std::vector<zip::Entry>& entries) {
    std::vector<std::string> shared = SharedStrings(zip::Find(entries, "xl/sharedStrings.xml"));
    std::vector<std::vector<std::string>> rows;
    XmlReader reader(zip::Find(entries, FirstSheetPath(entries)));
    Tag tag;
    std::string text;
    std::string type;
    std::string value;
    size_t column = 0;
    size_t next = 0;
    bool inValue = false;
    while (reader.Next(&tag, &text)) {
        if (inValue) {
            value += text;
        }
        if (tag.name == "row" && !tag.closing) {
            rows.emplace_back();
            next = 0;
        } else if (tag.name == "c" && !tag.closing) {
            column = ColumnOf(Attribute(tag, "r"), next);
            next = column + 1;
            type = Attribute(tag, "t");
            value.clear();
            if (tag.empty && !rows.empty()) {
                continue;
            }
        } else if (tag.name == "c" && tag.closing && !rows.empty()) {
            if (type == "s") {
                size_t index = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
                value = index < shared.size() ? shared[index] : std::string();
            } else if (type == "b") {
                value = value == "1" ? "TRUE" : "FALSE";
            }
            Place(rows.back(), column, value);
        } else if (tag.name == "v" || tag.name == "t") {
            inValue = !tag.closing && !tag.empty;
        }
    }
    return rows;
}

// --- LibreOffice --------------------------------------------------------

std::vector<std::vector<std::string>> ReadOds(const std::vector<zip::Entry>& entries) {
    std::vector<std::vector<std::string>> rows;
    XmlReader reader(zip::Find(entries, "content.xml"));
    Tag tag;
    std::string text;
    std::string value;
    std::string numeric;
    size_t column = 0;
    size_t repeat = 1;
    int paragraphs = 0;
    bool inCell = false;
    bool inTable = false;
    while (reader.Next(&tag, &text)) {
        if (inCell && paragraphs > 0) {
            value += text;
        }
        const std::string& name = tag.name;
        if (name == "table:table") {
            if (tag.closing) {
                break;
            }
            inTable = true;
        } else if (!inTable) {
            continue;
        } else if (name == "table:table-row" && !tag.closing) {
            rows.emplace_back();
            column = 0;
        } else if ((name == "table:table-cell" || name == "table:covered-table-cell") &&
                   !tag.closing) {
            std::string repeated = Attribute(tag, "table:number-columns-repeated");
            repeat = repeated.empty() ? 1 : std::max<size_t>(1, std::strtoul(repeated.c_str(), nullptr, 10));
            std::string kind = Attribute(tag, "office:value-type");
            numeric = kind == "float" || kind == "percentage" || kind == "currency"
                          ? Attribute(tag, "office:value")
                          : std::string();
            value.clear();
            paragraphs = 0;
            inCell = !tag.empty;
            if (tag.empty) {
                column += repeat;
            }
        } else if ((name == "table:table-cell" || name == "table:covered-table-cell") &&
                   tag.closing) {
            std::string cell = numeric.empty() ? value : numeric;
            // A repeated cell with content stands for as many copies; an empty
            // run, often thousands long, only moves the column.
            for (size_t copy = 0; copy < repeat && !cell.empty() && copy < 64; ++copy) {
                if (!rows.empty()) {
                    Place(rows.back(), column + copy, cell);
                }
            }
            column += repeat;
            inCell = false;
        } else if (inCell && name == "text:p") {
            if (!tag.closing && !value.empty() && paragraphs == 0) {
                value += '\n';
            }
            paragraphs += tag.closing ? -1 : (tag.empty ? 0 : 1);
        } else if (inCell && name == "text:s" && paragraphs > 0) {
            std::string count = Attribute(tag, "text:c");
            value.append(count.empty() ? 1 : std::strtoul(count.c_str(), nullptr, 10), ' ');
        } else if (inCell && name == "text:tab" && paragraphs > 0) {
            value += '\t';
        } else if (inCell && name == "text:line-break" && paragraphs > 0) {
            value += '\n';
        }
    }
    return rows;
}

}  // namespace

namespace sheet {

std::string WriteXlsx(const Rows& rows) {
    std::string types = std::string(kXmlHead) +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>";
    std::string packageRelations = std::string(kXmlHead) +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
    std::string workbook = std::string(kXmlHead) +
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"anime-dm\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
    std::string workbookRelations = std::string(kXmlHead) +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        "</Relationships>";
    std::string styles = std::string(kXmlHead) +
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"2\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/></cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>";

    std::string sheet = std::string(kXmlHead) +
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<sheetViews><sheetView workbookViewId=\"0\">"
        "<pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
        "</sheetView></sheetViews>";
    std::vector<size_t> widths = Widths(rows);
    if (!widths.empty()) {
        sheet += "<cols>";
        for (size_t column = 0; column < widths.size(); ++column) {
            std::string index = std::to_string(column + 1);
            sheet += "<col min=\"" + index + "\" max=\"" + index + "\" width=\"" +
                     std::to_string(widths[column]) + "\" customWidth=\"1\"/>";
        }
        sheet += "</cols>";
    }
    sheet += "<sheetData>";
    for (size_t line = 0; line < rows.size(); ++line) {
        std::string number = std::to_string(line + 1);
        sheet += "<row r=\"" + number + "\">";
        for (size_t column = 0; column < rows[line].size(); ++column) {
            const Cell& cell = rows[line][column];
            if (cell.text.empty()) {
                continue;
            }
            std::string reference = ColumnName(column) + number;
            std::string style = line == 0 ? " s=\"1\"" : "";
            if (cell.number) {
                sheet += "<c r=\"" + reference + "\"" + style + "><v>" + Escape(cell.text) + "</v></c>";
            } else {
                sheet += "<c r=\"" + reference + "\"" + style +
                         " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + Escape(cell.text) +
                         "</t></is></c>";
            }
        }
        sheet += "</row>";
    }
    sheet += "</sheetData></worksheet>";

    return zip::Write({
        {"[Content_Types].xml", types},
        {"_rels/.rels", packageRelations},
        {"xl/workbook.xml", workbook},
        {"xl/_rels/workbook.xml.rels", workbookRelations},
        {"xl/styles.xml", styles},
        {"xl/worksheets/sheet1.xml", sheet},
    });
}

std::string WriteOds(const Rows& rows) {
    std::string manifest = std::string(kXmlHead) +
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.2\" manifest:media-type=\"" +
        std::string(kOdsType) + "\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "</manifest:manifest>";

    std::vector<size_t> widths = Widths(rows);
    std::string content = std::string(kXmlHead) +
        "<office:document-content "
        "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
        "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
        "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
        "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
        "office:version=\"1.2\"><office:automatic-styles>";
    for (size_t column = 0; column < widths.size(); ++column) {
        // A character of the default font is about two millimetres wide.
        char width[32] = {};
        snprintf(width, sizeof(width), "%.2fcm", static_cast<double>(widths[column]) * 0.2);
        content += "<style:style style:name=\"co" + std::to_string(column + 1) +
                   "\" style:family=\"table-column\"><style:table-column-properties style:column-width=\"" +
                   width + "\"/></style:style>";
    }
    content +=
        "<style:style style:name=\"ce1\" style:family=\"table-cell\">"
        "<style:text-properties fo:font-weight=\"bold\" style:font-weight-asian=\"bold\" "
        "style:font-weight-complex=\"bold\"/></style:style>"
        "</office:automatic-styles><office:body><office:spreadsheet>"
        "<table:table table:name=\"anime-dm\">";
    for (size_t column = 0; column < widths.size(); ++column) {
        content += "<table:table-column table:style-name=\"co" + std::to_string(column + 1) + "\"/>";
    }
    for (size_t line = 0; line < rows.size(); ++line) {
        content += "<table:table-row>";
        for (const Cell& cell : rows[line]) {
            std::string style = line == 0 ? " table:style-name=\"ce1\"" : "";
            if (cell.text.empty()) {
                content += "<table:table-cell" + style + "/>";
            } else if (cell.number) {
                content += "<table:table-cell" + style + " office:value-type=\"float\" office:value=\"" +
                           Escape(cell.text) + "\"><text:p>" + Escape(cell.text) +
                           "</text:p></table:table-cell>";
            } else {
                content += "<table:table-cell" + style + " office:value-type=\"string\"><text:p>" +
                           Escape(cell.text) + "</text:p></table:table-cell>";
            }
        }
        content += "</table:table-row>";
    }
    content += "</table:table></office:spreadsheet></office:body></office:document-content>";

    // The media type comes first and uncompressed, where readers look for it.
    return zip::Write({
        {"mimetype", kOdsType},
        {"META-INF/manifest.xml", manifest},
        {"content.xml", content},
    });
}

std::vector<std::vector<std::string>> Read(const std::string& bytes) {
    if (!zip::IsZip(bytes)) {
        return {};
    }
    std::vector<zip::Entry> entries = zip::Read(bytes);
    if (!zip::Find(entries, "content.xml").empty()) {
        return ReadOds(entries);
    }
    if (!zip::Find(entries, "xl/workbook.xml").empty()) {
        return ReadXlsx(entries);
    }
    return {};
}

}  // namespace sheet

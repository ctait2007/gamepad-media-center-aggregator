/*
    GMCA — the language catalog (app/src/api/media/langs.cpp).

    Two things matter here and neither is obvious from the table:

      * the catalog spells its regional variants the way NuvioTV does
        ("zh-CN"), while a lookup arrives lowercased, so the index has to be
        normalised on the way IN or "zh-cn" falls through the prefix fallback
        to plain Chinese and the Simplified/Traditional distinction is lost;

      * langMatchList() is what reaches mpv's `alang`, so it has to carry the
        aliases -- a track tagged "eng" and one tagged "English" are both the
        English the viewer asked for.
*/

#include <cassert>
#include <cstdio>
#include <string>

#include "../app/src/api/media/langs.cpp"

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

int main() {
    // The reference's own catalog size and order.
    const auto& cat = media::subtitleLangCatalog();
    assert(cat.size() == 78);
    assert(cat.front().display == "Afrikaans");
    assert(cat.back().display == "Zulu");

    // Codes, 639-2/B and /T, and English names all resolve.
    assert(media::subtitleLangCode("en") == "en");
    assert(media::subtitleLangCode("eng") == "en");
    assert(media::subtitleLangCode("English") == "en");
    assert(media::subtitleLangCode("  EN  ") == "en");
    assert(media::subtitleLangDisplay("eng") == "English");

    // Regional variants stay distinct from their parent.
    assert(media::subtitleLangCode("zh-CN") == "zh-CN");
    assert(media::subtitleLangCode("zh-cn") == "zh-CN");
    assert(media::subtitleLangCode("zh-Hans") == "zh-CN");
    assert(media::subtitleLangCode("zh-TW") == "zh-TW");
    assert(media::subtitleLangCode("zh") == "zh");
    assert(media::subtitleLangCode("pob") == "pt-br");
    assert(media::subtitleLangCode("es-419") == "es-419");

    // An unknown code keeps its raw text rather than disappearing.
    assert(media::subtitleLangCode("klingon").empty());
    assert(media::subtitleLangDisplay(" klingon ") == "klingon");

    // What mpv gets for `alang`: the code first, then every alias we would
    // recognise, and nothing with a space in it (mpv takes a comma list).
    const std::string en = media::langMatchList("English");
    assert(en.substr(0, 3) == "en,");
    assert(contains(en, ",eng"));
    assert(contains(en, ",english"));
    assert(en.find(' ') == std::string::npos);

    const std::string ptbr = media::langMatchList("pt-br");
    assert(ptbr.substr(0, 6) == "pt-br,");
    assert(contains(ptbr, ",pob"));
    // "brazilian portuguese" has a space, so it is left out.
    assert(ptbr.find(' ') == std::string::npos);

    assert(media::langMatchList("klingon").empty());

    std::printf("test_langs: OK (%zu languages)\n", cat.size());
    return 0;
}

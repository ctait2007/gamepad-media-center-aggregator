/*
    GMCA — the content warnings NuvioTV shows over the first seconds of
    playback (ParentalGuideOverlay / ParentalGuideRepository).

    Its source is the same one, and it needs no key either:

        GET https://api.tiffara.com/titles/tt0903747/parentsGuide

        {"parentsGuide":[
           {"category":"VIOLENCE",
            "severityBreakdowns":[{"severityLevel":"severe","voteCount":1523},
                                  {"severityLevel":"none","voteCount":63}, …]}, …]}

    Five of its categories are used, and each is reduced to ONE severity the
    way the reference reduces it: the level with the most votes, "none"
    excluded — and the category is dropped entirely when "none" outvotes it.
    A crowd that mostly voted "no violence here" is not a content warning.
*/

#pragma once

#include <api/media/types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace parental {

/// One line of the overlay: "Violence · Severe", both halves already
/// localised.
struct Warning {
    std::string label;
    std::string severity;
};

/// GET the guide for `item`'s IMDb id. `then` runs on the UI thread with the
/// warnings in the reference's own order — severe, then moderate, then mild,
/// at most five — or an empty list, which is the ordinary case for anything
/// nobody has rated.
///
/// Off entirely unless MPVCore::PARENTAL_GUIDE, and never asked for an item
/// with no IMDb id (the lookup is by title, so an episode uses its show's).
void fetch(const media::Item& item, std::function<void(std::vector<Warning>)> then);

/// Forget every cached lookup. Only for tests.
void clearCache();

}  // namespace parental

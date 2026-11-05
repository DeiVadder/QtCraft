// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtAssert>

#include <concepts>
#include <functional>
#include <iterator>
#include <ranges>
#include <type_traits>

// Choice by relative weight (decision E-977): world generation, loot, spawning and enchanting all
// draw one entry of a list, each entry as likely as its weight. The draw itself (a number between
// zero and the total) stays with the caller, so each keeps its own random source.

/*! The type of the weight \a WeightOf gives an element of \a Range (a member pointer or a
    function of an element), without reference and const. */
template <typename Range, typename WeightOf>
using QcWeightOf = std::remove_cvref_t<std::invoke_result_t<WeightOf &, std::ranges::range_reference_t<const Range>>>;

/*! Returns the sum of the weights of the elements of \a range, as \a weightOf gives them (a member
    pointer or a function of an element), added up in the order of the range. */
template <std::ranges::input_range Range, typename WeightOf>
[[nodiscard]] constexpr QcWeightOf<Range, WeightOf> qcWeightTotal(const Range &range, WeightOf weightOf)
{
    QcWeightOf<Range, WeightOf> total{};
    for (const auto &element : range) {
        total += std::invoke(weightOf, element);
    }
    return total;
}

/*! Returns the element of \a range that \a pick falls on when the weights (\a weightOf of each
    element) are laid end to end in the order of the range: the first one whose running sum of
    weights exceeds \a pick. An element of weight 0 is never chosen. Returns the end of the range
    when \a pick is not below the total. \a pick has the type of the weights, so the comparison
    rounds nothing. */
template <std::ranges::forward_range Range, typename Pick, typename WeightOf>
    requires std::same_as<Pick, QcWeightOf<Range, WeightOf>>
[[nodiscard]] constexpr std::ranges::iterator_t<const Range> qcWeightedFind(const Range &range, Pick pick, WeightOf weightOf)
{
    auto it = std::ranges::begin(range);
    const auto end = std::ranges::end(range);
    for (; it != end; ++it) {
        pick -= std::invoke(weightOf, *it);
        if (pick < Pick{}) {
            break;
        }
    }
    return it;
}

/*! Returns the element of the non-empty \a range that \a random (in [0, 1)) chooses, each element
    as likely as its weight (\a weightOf of it). The draw is \a random times the total weight,
    converted to the type of the weights (an integer weight rounds towards zero). When the draw
    reaches no element (a total of 0, or rounding carries it to the total), the last element is
    chosen. The range holds its elements (its references are lvalue references), so the returned
    reference stays valid as long as the range. */
template <std::ranges::bidirectional_range Range, typename WeightOf>
    requires std::is_lvalue_reference_v<std::ranges::range_reference_t<const Range>>
[[nodiscard]] constexpr const auto &qcWeightedPick(const Range &range, double random, WeightOf weightOf)
{
    Q_ASSERT(!std::ranges::empty(range));
    using Weight = QcWeightOf<Range, WeightOf>;
    const auto pick = static_cast<Weight>(random * qcWeightTotal(range, weightOf));
    const auto chosen = qcWeightedFind(range, pick, weightOf);
    return chosen != std::ranges::end(range) ? *chosen : *std::ranges::prev(std::ranges::end(range));
}

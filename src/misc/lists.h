#pragma once

#include <algorithm>
#include <concepts>
#include <forward_list>
#include <functional>
#include <iterator>
#include <ostream>
#include <ranges>

namespace misc
{

///
template <typename C>
concept HasIntegralSize = requires(C c) {
    { c.size() } -> std::integral;
};

///
template <class T>
concept Integral = std::is_integral<T>::value;

///
template <typename C, typename I>
concept HasWeaklyIncrementableIterator = Integral<I> && requires(C c, I i) {
    { c.insert_iterator(i) } -> std::weakly_incrementable;
};

///
template <typename T>
class SortedForwardList
{
  public:
    constexpr std::forward_list<T>::const_iterator begin () const
    { return list_.begin(); }
    constexpr std::forward_list<T>::const_iterator end () const
    { return list_.end(); }

    // Inserts a value into list in sorted order according to given
    // projection proj. It's not allowed to have duplicates values in the list.
    template <class Proj = std::identity>
    void insert (T &&value, Proj proj = {})
    {
        namespace rng = std::ranges;
        auto &eq_val  = std::invoke(proj, value);
        if (rng::find(list_, eq_val, proj) != rng::end(list_)) return;
        list_.insert_after(
          rng::next(
            list_.before_begin(),
            rng::distance(rng::begin(list_),
                          rng::find_if(
                            list_, [&eq_val] (auto const &v) { return eq_val < v; }, proj))),
          std::forward<T>(value));
    };

    // Extrcts at least C::size() sequential elements from the list starting from
    // the element in C projected by proj, immediately following the sequence
    // number given in min_val. All elements which are less or equal to min_val
    // removed from the list. The elements following extracted sequence are
    // shifted in the front of the list.
    template <typename C, typename V, class Proj = std::identity>
        requires HasIntegralSize<C> && HasWeaklyIncrementableIterator<C, V>
    bool extract_some (C &dest, V const &min_val, Proj proj = {})
    {
        namespace rng = std::ranges;
        auto ret =
          rng::partition(list_, [&min_val] (V const &v) { return min_val >= v; }, proj);

        if (list_.erase_after(list_.before_begin(),
                              rng::next(list_.before_begin(),
                                        rng::distance(rng::begin(list_), rng::begin(ret)) +
                                          1)) != list_.end()) {

            if (std::invoke(proj, list_.front()) == min_val + 1) {

                ret = rng::partition(
                  list_,
                  [v0 = min_val + 1] (V const &v1) mutable {
                      return v0 == v1 ? (++v0, true) : false;
                  },
                  proj);

                if (auto count =
                      std::min(size_t(rng::distance(rng::begin(list_), rng::begin(ret))),
                               dest.size())) {
                    auto ret1 =
                      rng::move(rng::begin(list_), rng::next(rng::begin(list_), count),
                                dest.insert_iterator(min_val));

                    list_.erase_after(
                      list_.before_begin(),
                      rng::next(list_.before_begin(),
                                rng::distance(rng::begin(list_), ret1.in) + 1));
                    return true;
                }
            }
        }
        return false;
    }

    void print (std::ostream &os) const
    {
        namespace rng = std::ranges;
        rng::copy(list_, std::ostream_iterator<T>(os, " "));
        os << '\n';
    }

  private:
    std::forward_list<T> list_;
};

} // namespace misc

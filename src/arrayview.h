// Compiler from Wolfram Language to C++
// 
// Copyright 2019 Tianhuan Lu
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once

#include <algorithm>
#include <exception>
#include <optional>
#include <tuple>

#include "types.h"
#include "traits.h"
#include "const.h"
#include "view_derive.h"

namespace wl
{

template<bool UnitStep>
struct indexer_iter;

template<>
struct indexer_iter<false>
{
    size_t index_;
    ptrdiff_t step_;

    indexer_iter(size_t index, ptrdiff_t step) :
        index_{index}, step_{step}
    {
    }

    auto& operator++()
    {
        index_ += step_;
        return *this;
    }

    bool operator==(const indexer_iter& other) const
    {
        return this->index_ == other.index_;
    }
};

template<>
struct indexer_iter<true>
{
    size_t index_;

    explicit indexer_iter(size_t index) :
        index_{index}
    {
    }

    auto& operator++()
    {
        ++index_;
        return *this;
    }

    bool operator==(const indexer_iter& other) const
    {
        return this->index_ == other.index_;
    }
};

template<typename IndexType>
size_t convert_index(const IndexType& idx, const size_t& dim)
{
    static_assert(is_integral_v<IndexType>, "badidxtype");
    if constexpr (std::is_unsigned_v<IndexType>)
    {
        if (1u <= idx && idx <= dim)
            return size_t(idx - 1u);
        else
            throw std::logic_error("badindex");
    }
    else
    {
        ptrdiff_t pos_idx = idx >= 0 ?
            idx : idx + ptrdiff_t(dim) + 1;
        if (1 <= pos_idx && pos_idx <= ptrdiff_t(dim))
            return size_t(pos_idx - 1u);
        else
            throw std::logic_error("badindex");
    }
    return 0u;
};

struct scalar_indexer
{
    size_t index_;

    scalar_indexer() = default;

    template<typename IndexType>
    scalar_indexer(IndexType index, size_t dim) :
        index_{convert_index(index, dim)}
    {
    }

    size_t offset() const
    {
        return this->index_;
    }

    size_t size() = delete;

    ptrdiff_t stride() = delete;
};

struct all_indexer
{
    size_t size_;

    all_indexer() = default;

    explicit all_indexer(size_t dim) :
        size_{dim}
    {
    }

    size_t offset() const
    {
        return 0u;
    }

    size_t size() const
    {
        return this->size_;
    }

    ptrdiff_t stride() const
    {
        return 1;
    }
};

struct unit_indexer
{
    size_t begin_;
    size_t size_;

    unit_indexer() = default;

    unit_indexer(size_t begin, size_t size) :
        begin_{begin}, size_{size}
    {
    }

    size_t offset() const
    {
        return this->begin_;
    }

    size_t size() const
    {
        return this->size_;
    }

    ptrdiff_t stride() const
    {
        return 1;
    }

    auto begin() const
    {
        return indexer_iter<true>(begin_);
    }

    auto end() const
    {
        return indexer_iter<true>(begin_ + size_);
    }
};

struct step_indexer
{
    size_t begin_;
    size_t size_;
    ptrdiff_t step_;

    step_indexer() = default;

    step_indexer(size_t begin, size_t size, ptrdiff_t step) :
        begin_{begin}, size_{size}, step_{step}
    {
    }

    size_t offset() const
    {
        return this->begin_;
    }

    size_t size() const
    {
        return this->size_;
    }

    ptrdiff_t stride() const
    {
        return step_;
    }

    auto begin() const
    {
        return indexer_iter<false>(begin_, step_);
    }

    auto end() const
    {
        return indexer_iter<false>(begin_ + step_ * size_, step_);
    }
};

struct list_indexer
{
    std::vector<size_t> indices_;

    list_indexer() = default;

    template<typename Iter>
    list_indexer(Iter idx_begin, Iter idx_end, size_t level_dim)
    {
        using IndexType = remove_cvref_t<decltype(*idx_begin)>;
        indices_.resize(idx_end - idx_begin);
        std::transform(idx_begin, idx_end, indices_.begin(),
            [&](const auto& idx) { return convert_index(idx, level_dim); });
    }

    size_t offset() const
    {
        return 0u;
    }

    ptrdiff_t stride() = delete;

    size_t size() const
    {
        return indices_.size();
    }

    const auto& indices() const
    {
        return this->indices_;
    }
};

template<typename Begin, typename End, typename Step>
struct span
{
    static constexpr auto default_begin = std::is_same_v<Begin, all_type>;
    static constexpr auto default_end = std::is_same_v<End, all_type>;
    static constexpr auto default_step = std::is_same_v<Step, all_type>;

    static_assert(default_begin || is_integral_v<Begin>, "badargtype");
    static_assert(default_end || is_integral_v<End>, "badargtype");
    static_assert(default_step || is_integral_v<Step>, "badargtype");

    Begin begin_;
    End end_;
    Step step_;

    span(Begin begin, End end, Step step) :
        begin_{begin}, end_{end}, step_{step}
    {
    }

    auto to_indexer(size_t dim) const
    {
        if constexpr (default_begin && default_end && default_step)
            return all_indexer(dim);
        else
        {
            // convert WL-style index to C-style index
            ptrdiff_t begin = 1;
            ptrdiff_t end = ptrdiff_t(dim);
            ptrdiff_t step = 1;
            if constexpr (!default_begin)
            {
                if constexpr (std::is_unsigned_v<Begin>)
                    begin = ptrdiff_t(this->begin_);
                else if (this->begin_ >= 0)
                    begin = ptrdiff_t(this->begin_);
                else // this->begin_ < 0
                    begin = ptrdiff_t(this->begin_) + ptrdiff_t(dim + 1u);
                //check out-of-bound
                if (begin < 1 || begin > ptrdiff_t(dim))
                    throw std::logic_error("badindex");
            }
            if constexpr (!default_end)
            {
                if constexpr (std::is_unsigned_v<End>)
                    end = ptrdiff_t(this->end_);
                else if (this->end_ >= 0)
                    end = ptrdiff_t(this->end_);
                else // this->end_ < 0
                    end = ptrdiff_t(this->end_) + ptrdiff_t(dim + 1u);
                //check out-of-bound
                if (end < 1 || end > ptrdiff_t(dim))
                    throw std::logic_error("badindex");
            }
            if constexpr (!default_step)
            {
                step = step_;
                if (step == 0)
                    throw std::logic_error("badstep");
            }

            if constexpr (default_step)
            {
                if constexpr (default_begin || default_end)
                {
                    return unit_indexer(
                        size_t(begin - 1),
                        size_t(end - begin + 1));
                }
                else
                {
                    return unit_indexer(
                        size_t(begin - 1),
                        end >= begin ? size_t(end - begin + 1) : 0u);
                }
            }
            else if (step > 0)
            {
                return step_indexer(
                    size_t(begin - 1),
                    end >= begin ? size_t((end - begin) / step) + 1u : 0u,
                    step);
            }
            else // step < 0
            {
                return step_indexer(
                    size_t(begin),
                    end <= begin ? size_t((begin - end) / -step) + 1u : 0u,
                    step);
            }
        }
    }
};

template<typename Begin, typename End, typename Step>
auto make_span(const Begin& begin, const End& end, const Step& step)
{
    return span<Begin, End, Step>(begin, end, step);
}

template<typename Begin, typename End>
auto make_span(const Begin& begin, const End& end)
{
    return make_span(begin, end, const_all);
}

template<typename Begin, typename End, typename Step>
auto make_indexer(const span<Begin, End, Step>& s, size_t dim)
{
    return s.to_indexer(dim);
}

template<typename IndexType, size_t Rank>
auto make_indexer(const ndarray<IndexType, Rank>& list, size_t dim)
{
    static_assert(Rank == 1u, "badargtype");
    static_assert(is_integral_v<IndexType>, "badidxtype");
    return list_indexer(list.begin(), list.end(), dim);
}

template<typename IndexType>
auto make_indexer(const IndexType& index, size_t dim)
{
    if constexpr (std::is_same_v<IndexType, all_type>)
        return all_indexer(dim);
    else
    {
        static_assert(is_integral_v<IndexType>, "badidxtype");
        return scalar_indexer(index, dim);
    }
}

template<size_t Rank>
bool _check_dims(const size_t* dims1, const size_t* dims2)
{
    static_assert(Rank >= 1u, "internal");
    if constexpr (Rank == 1u)
        return *dims1 == *dims2;
    else
        return (*dims1 == *dims2) &&
        _check_dims<Rank - 1u>(dims1 + 1, dims2 + 1);
}

template<typename T, size_t ArrayRank, size_t ViewRank, bool Const>
struct simple_view
{
    static constexpr auto is_const = Const;
    static constexpr auto array_rank = ArrayRank;
    static constexpr auto view_rank = ViewRank;
    static constexpr auto rank = ViewRank;
    static constexpr auto category = view_category::Simple;

    using value_type = T;
    using array_ref_type = std::conditional_t<Const,
        const ndarray<T, ArrayRank>&, ndarray<T, ArrayRank>&>;
    using pointer_type = std::conditional_t<Const, const T*, T*>;
    using _dims_t = std::array<size_t, ViewRank>;

    const T* const identifier_;
    pointer_type data_;
    _dims_t dims_;
    size_t size_;

    template<typename... Specs>
    simple_view(array_ref_type base, const Specs&... specs) :
        identifier_{base.data()}, size_{1u}
    {
        static_assert(sizeof...(Specs) == ArrayRank, "internal");
        size_t offset = 0u;
        this->_initialize<0u>(offset, base.dims_ptr(), specs...);
        this->data_ = base.data() + offset;
    }

    template<size_t Level, typename Spec1, typename... Specs>
    void _initialize(size_t& offset, const size_t* dims,
        const Spec1& spec1, const Specs&... specs)
    {
        if constexpr (Level > 0)
            offset *= dims[Level];
        offset += spec1.offset();

        if constexpr (Level < ArrayRank - ViewRank)
        {
            static_assert(std::is_same_v<Spec1, scalar_indexer>, "internal");
            this->_initialize<Level + 1u>(offset, dims, specs...);
        }
        else
        {
            static_assert(!std::is_same_v<Spec1, scalar_indexer>, "internal");
            constexpr size_t ViewLevel = Level - (ArrayRank - ViewRank);
            this->dims_[ViewLevel] = spec1.size();
            this->size_ *= this->dims_[ViewLevel];
            if constexpr (Level < ArrayRank - 1u)
                _initialize<Level + 1u>(offset, dims, specs...);
        }
    }

    const size_t* dims_ptr() const
    {
        return this->dims_.data();
    }

    auto identifier() const
    {
        return this->identifier_;
    }

    auto begin() const
    {
        return this->data_;
    }

    auto data() const
    {
        return this->data_;
    }

    auto size() const
    {
        return this->size_;
    }

    template<typename Function>
    void for_each(Function f) const
    {
        auto ptr = this->data_;
        for (size_t i = 0u; i < this->size_; ++i, ++ptr)
            f(*ptr);
    }

    template<typename FwdIter>
    void copy_to(FwdIter iter) const
    {
        auto ptr = this->data_;
        for (size_t i = 0u; i < this->size_; ++i, ++ptr, ++iter)
            *iter = *ptr;
    }

    template<typename FwdIter>
    void copy_from(FwdIter iter) const
    {
        auto ptr = this->data_;
        for (size_t i = 0u; i < this->size_; ++i, ++ptr, ++iter)
            *ptr = *iter;
    }

    auto to_array() const
    {
        return ndarray<T, ViewRank>(this->dims_, this->begin());
    }
};

template<typename T, bool Const>
struct regular_view_iterator
{
    using _my_type = regular_view_iterator;
    using value_type = T;
    using pointer_type = std::conditional_t<Const, const T*, T*>;

    pointer_type pointer_;
    size_t stride_;

    regular_view_iterator(pointer_type pointer, size_t stride) :
        pointer_{pointer}, stride_{stride}
    {
    }

    auto& operator*()
    {
        return *this->pointer_;
    }

    auto operator*() const
    {
        return *this->pointer_;
    }

    auto operator++()
    {
        this->pointer_ += this->stride_;
        return *this;
    }

    auto operator==(const _my_type& other)
    {
        return this->pointer_ == other.pointer_;
    }

    auto operator+(ptrdiff_t diff) const
    {
        return _my_type(pointer_ + diff * stride_, stride_);
    }
};

template<typename T, size_t ArrayRank, size_t ViewRank, size_t StrideRank, bool Const>
struct regular_view
{
    static constexpr auto is_const = Const;
    static constexpr auto array_rank = ArrayRank;
    static constexpr auto view_rank = ViewRank;
    static constexpr auto stride_rank = StrideRank;
    static constexpr auto rank = ViewRank;
    static constexpr auto category = view_category::Regular;

    using value_type = T;
    using array_ref_type = std::conditional_t<Const,
        const ndarray<T, ArrayRank>&, ndarray<T, ArrayRank>&>;
    using pointer_type = std::conditional_t<Const, const T*, T*>;
    using _dims_t = std::array<size_t, ViewRank>;

    const T* const identifier_;
    pointer_type data_;
    ptrdiff_t stride_;
    _dims_t dims_;
    size_t size_;

    template<typename... Specs>
    regular_view(array_ref_type base, const Specs&... specs) :
        identifier_{base.data()}, size_{1u}
    {
        static_assert(sizeof...(Specs) == ArrayRank, "internal");
        this->stride_ = 1;
        size_t offset = 0u;
        this->_initialize<0u>(offset, base.dims_ptr(), specs...);
        this->data_ = base.data() + offset;
    }

    template<size_t Level, typename Spec1, typename... Specs>
    auto _initialize(size_t& offset, const size_t* dims,
        const Spec1& spec1, const Specs&... specs)
    {
        if constexpr (Level > 0)
            offset *= dims[Level];
        offset += spec1.offset();

        if constexpr (Level < ArrayRank - ViewRank - StrideRank)
        {
            static_assert(std::is_same_v<Spec1, scalar_indexer>, "internal");
            this->_initialize<Level + 1>(offset, dims, specs...);
        }
        else if constexpr (Level < ArrayRank - StrideRank)
        {
            static_assert(!std::is_same_v<Spec1, scalar_indexer>, "internal");
            constexpr size_t ViewLevel = Level -
                (ArrayRank - ViewRank - StrideRank);
            this->dims_[ViewLevel] = spec1.size();
            this->size_ *= this->dims_[ViewLevel];
            if constexpr (std::is_same_v<Spec1, step_indexer>)
                this->stride_ *= spec1.stride();
            if constexpr (Level < ArrayRank - 1u)
                _initialize<Level + 1u>(offset, dims, specs...);
        }
        else
        {
            static_assert(std::is_same_v<Spec1, scalar_indexer>, "internal");
            this->stride_ *= dims[Level];
            if constexpr (Level < ArrayRank - 1u)
                _initialize<Level + 1u>(offset, dims, specs...);
        }
    }

    const size_t* dims_ptr() const
    {
        return this->dims_.data();
    }

    auto identifier() const
    {
        return this->identifier_;
    }

    auto begin() const
    {
        return regular_view_iterator<T, Const>(this->data_, this->stride_);
    }

    auto data() const
    {
        return this->data_;
    }

    auto size() const
    {
        return this->size_;
    }

    auto stride() const
    {
        return this->stride_;
    }

    template<typename Function>
    void for_each(Function f) const
    {
        auto ptr = this->data_;
        for (size_t i = 0u; i < this->size_; ++i, ptr += this->stride_)
            f(*ptr);
    }

    template<typename FwdIter>
    void copy_to(FwdIter iter) const
    {
        auto ptr = this->data_;
        for (size_t i = 0u; i < this->size_; ++i, ptr += this->stride_, ++iter)
            *iter = *ptr;
    }

    template<typename FwdIter>
    void copy_from(FwdIter iter) const
    {
        auto ptr = this->data_;
        for (size_t i = 0u; i < this->size_; ++i, ptr += this->stride_, ++iter)
            *ptr = *iter;
    }

    auto to_array() const
    {
        return ndarray<T, ViewRank>(this->dims_, this->begin());
    }
};

template<typename T, size_t ArrayRank, size_t ViewRank, size_t StrideRank, typename IndexersTuple, bool Const>
struct general_view
{
    static_assert(ViewRank == std::tuple_size_v<IndexersTuple>, "internal");

    static constexpr auto is_const = Const;
    static constexpr auto array_rank = ArrayRank;
    static constexpr auto view_rank = ViewRank;
    static constexpr auto stride_rank = StrideRank;
    static constexpr auto rank = ViewRank;
    static constexpr auto category = view_category::General;

    using value_type = T;
    using array_ref_type = std::conditional_t<Const,
        const ndarray<T, ArrayRank>&, ndarray<T, ArrayRank>&>;
    using pointer_type = std::conditional_t<Const, const T*, T*>;
    using _dims_t = std::array<size_t, ViewRank>;
    using _strides_t = std::array<ptrdiff_t, ViewRank>;
    using _indexers_tuple = IndexersTuple;

    static constexpr auto _has_last_stride = (StrideRank != 0) ||
        std::is_same_v<
        std::tuple_element_t<ViewRank - 1u, IndexersTuple>,
        step_indexer>;

    const T* const identifier_;
    pointer_type data_;
    _strides_t strides_;
    _indexers_tuple indexers_;
    _dims_t dims_;
    size_t size_;

    template<typename... Specs>
    general_view(array_ref_type base, const Specs&... specs) :
        identifier_{base.data()}, size_{1u}
    {
        static_assert(sizeof...(Specs) == ArrayRank, "internal");
        size_t offset = 0u;
        this->_initialize<0u, 0u>(offset, base.dims_ptr(), specs...);
        this->data_ = base.data() + offset;
    }

    template<size_t Level, size_t ViewLevel, typename Spec1, typename... Specs>
    auto _initialize(size_t& offset, const size_t* dims,
        const Spec1& spec1, const Specs&... specs)
    {
        if constexpr (Level > 0)
            offset *= dims[Level];
        offset += spec1.offset();

        if constexpr (std::is_same_v<Spec1, scalar_indexer>)
        {
            if constexpr (ViewLevel > 0u)
                this->strides_[ViewLevel - 1] *= dims[Level];
            if constexpr (Level < ArrayRank - 1u)
                _initialize<Level + 1u, ViewLevel>(offset, dims, specs...);
        }
        else
        {
            if constexpr (ViewLevel > 0u)
                this->strides_[ViewLevel - 1] *= dims[Level];
            if constexpr (std::is_same_v<Spec1, step_indexer>)
                this->strides_[ViewLevel] = spec1.stride();
            else
                this->strides_[ViewLevel] = 1;
            this->dims_[ViewLevel] = spec1.size();
            this->size_ *= this->dims_[ViewLevel];
            std::get<ViewLevel>(this->indexers_) = spec1;
            if constexpr (Level < ArrayRank - 1u)
                _initialize<Level + 1u, ViewLevel + 1u>(
                    offset, dims, specs...);
        }
    }

    const size_t* dims_ptr() const
    {
        return this->dims_.data();
    }

    auto identifier() const
    {
        return this->identifier_;
    }

    auto begin() const = delete;

    auto data() const = delete;

    auto size() const = delete;

    template<size_t ViewLevel, typename Function>
    void _for_each_impl(size_t index, Function& f) const
    {
        const auto& indexer = std::get<ViewLevel>(this->indexers_);
        using Indexer = remove_cvref_t<decltype(indexer)>;

        if constexpr (ViewLevel > 0u)
            index *= this->strides_[ViewLevel - 1];
        if constexpr (ViewLevel < ViewRank - 1u)
        {
            if constexpr (std::is_same_v<Indexer, list_indexer>)
                for (const auto& i : indexer.indices())
                    _for_each_impl<ViewLevel + 1u>(index + i, f);
            else
                for (size_t i = 0; i < this->dims_[ViewLevel]; ++i)
                    _for_each_impl<ViewLevel + 1u>(index + i, f);
        }
        else if constexpr (_has_last_stride)
        {
            const auto last_stride = this->strides_[ViewLevel];
            if constexpr (std::is_same_v<Indexer, list_indexer>)
                for (const auto& i : indexer.indices())
                    f(this->data_[(index + i) * last_stride]);
            else
                for (size_t i = 0; i < this->dims_[ViewLevel]; ++i)
                    f(this->data_[(index + i) * last_stride]);
        }
        else
        {
            if constexpr (std::is_same_v<Indexer, list_indexer>)
                for (const auto& i : indexer.indices())
                    f(this->data_[index + i]);
            else
                for (size_t i = 0; i < this->dims_[ViewLevel]; ++i)
                    f(this->data_[index + i]);
        }
    }

    template<typename Function>
    void for_each(Function f) const
    {
        this->_for_each_impl(0u, f);
    }

    template<typename FwdIter>
    void copy_to(FwdIter iter) const
    {
        this->for_each(
            [iter](const auto& val) mutable { *iter = val; ++iter; });
    }

    template<typename FwdIter>
    void copy_from(FwdIter iter) const
    {
        this->for_each(
            [iter](auto& val) mutable { val = *iter; ++iter; });
    }

    auto to_array() const
    {
        auto ret = ndarray<T, ViewRank>(this->dims_);
        this->copy_to(ret.begin());
        return ret;
    }
};

template<typename T, size_t ArrayRank, size_t ViewRank, bool Const>
auto view_guard(const simple_view<T, ArrayRank, ViewRank, Const>& view)
{
    return view.to_array();
}

template<typename Any>
auto view_guard(Any&& any)
{
    if constexpr (is_array_view_v<remove_cvref_t<Any>>)
        return std::forward<decltype(any)>(any).to_array();
    else
        return std::forward<decltype(any)>(any);
}

template<typename T>
struct scalar_view_iterator
{
    using _my_type = scalar_view_iterator;
    using value_type = T;

    T val_;

    constexpr scalar_view_iterator(const T& val) : val_{val}
    {
    }

    constexpr auto operator++() const
    {
        return *this;
    }

    auto operator==(const _my_type&) = delete;

    constexpr const T& operator*() const
    {
        return val_;
    }
};

template<typename T>
constexpr auto make_scalar_view_iterator(const T& val)
{
    return scalar_view_iterator<T>(val);
}

template<typename View>
auto _data_coverage(const View& view)
{
    if constexpr (View::category == view_category::Simple)
        return std::make_pair(view.data(), view.data() + view.size() - 1u);
    else
    {
        static_assert(View::category == view_category::Regular, "internal");
        return std::make_pair(view.data(),
            view.data() + (view.size() - 1u) * view.stride() +
            (view.stride() > 0 ? 1 : -1));
    }
}

template<typename Dst, typename Src>
auto has_aliasing(const Dst& dst, const Src& src)
{
    if (dst.identifier() != src.identifier())
        return false;
    if constexpr (
        Dst::category == view_category::Array   ||
        Dst::category == view_category::General ||
        Src::category == view_category::Array   ||
        Src::category == view_category::General)
    {
        return true;
    }
    else
    {
        auto[src_min, src_max] = _data_coverage(src);
        auto[dst_min, dst_max] = _data_coverage(dst);
        return (src_max >= dst_min) && (src_min <= dst_max);
    }
}

template<typename Dst, typename Src>
auto indirect_view_copy(Dst&& dst, const Src& src)
{
    using SrcValueType = typename Src::value_type;
    using DstValueType = typename remove_cvref_t<Dst>::value_type;
    using T = std::conditional_t<sizeof(SrcValueType) < sizeof(DstValueType),
        SrcValueType, DstValueType>;

    std::vector<T> buffer(src.size());
    src.copy_to(buffer.begin());
    std::forward<decltype(dst)>(dst).copy_from(buffer.begin());
}

}

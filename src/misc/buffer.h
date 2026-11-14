#pragma once

#include <string>
#include <span>
#include <ostream>
#include <memory>
#include <iomanip>
#include <chrono>

namespace misc
{

class BoundedBuffer
{
    friend std::ostream& operator<<(std::ostream& os, BoundedBuffer const& b)
    {
        os << '{'
           << std::quoted(b.size() ? std::string_view{b.begin(), b.end()} : std::string_view{})
           << ',' << b.size() << '}';
        return os;
    }

  public:
    BoundedBuffer () = default;

    explicit BoundedBuffer (size_t capacity)
      : buffer_{std::make_shared_for_overwrite<char[]>(capacity)},
        data_{buffer_.get(), capacity}
    {
    }

    BoundedBuffer (const char* data)
      : BoundedBuffer(std::strlen(data))
    { std::memcpy(buffer_.get(), data, data_.size()); }

    BoundedBuffer (void* data, size_t length)
      : BoundedBuffer(length)
    { std::memcpy(buffer_.get(), data, length); }

    BoundedBuffer (BoundedBuffer const&)           = default;
    BoundedBuffer& operator=(BoundedBuffer const&) = default;

    BoundedBuffer (BoundedBuffer&& o)
      : buffer_{std::move(o.buffer_)},
        data_{o.data_}
    { o.data_ = {}; }

    BoundedBuffer& operator=(BoundedBuffer&& o)
    {
        if (&o != this) {
            buffer_ = std::move(o.buffer_);
            data_   = o.data_;
            o.data_ = {};
        }
        return *this;
    }

    constexpr BoundedBuffer& reset (void* data, size_t length)
    {
        buffer_.reset(static_cast<char*>(data));
        data_ = std::span<char>{buffer_.get(), length};
        return *this;
    }

    constexpr operator bool () const
    { return data_.size() != 0; }

    constexpr size_t size () const
    { return data_.size(); }

    constexpr const char* begin () const
    { return data_.data(); }

    constexpr char* begin ()
    { return data_.data(); }

    constexpr const char* end () const
    { return data_.data() + data_.size(); }

    BoundedBuffer& operator-=(size_t size)
    {
        if (data_.size() > size)
            data_ = std::span<char>{data_.data() + size, data_.size() - size};
        else
            data_ = std::span<char>{};
        return *this;
    }

  private:
    std::shared_ptr<char[]> buffer_;
    std::span<char> data_;
};

#pragma pack(push, 1)
struct PacketBuffer
{
    BoundedBuffer buffer_;
    std::string local_namespace_;
    std::chrono::system_clock::rep time_us_;
    uint16_t feed_group_;
    bool recovery_flag_{false};
};
#pragma pack(pop)

}

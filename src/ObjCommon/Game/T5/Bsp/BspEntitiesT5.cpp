#include "BspEntitiesT5.h"

#include "Utils/StringUtils.h"

#include <charconv>
#include <cstdlib>
#include <format>

namespace
{
    class EntityTokenizer
    {
    public:
        explicit EntityTokenizer(const std::string& text)
            : m_text(text),
              m_pos(0u)
        {
        }

        std::optional<std::string> Next()
        {
            SkipWhitespaceAndComments();
            if (m_pos >= m_text.size() || m_text[m_pos] == '\0')
                return std::nullopt;

            if (m_text[m_pos] == '"')
            {
                const auto end = m_text.find('"', m_pos + 1u);
                if (end == std::string::npos)
                    return std::nullopt;

                auto token = m_text.substr(m_pos + 1u, end - m_pos - 1u);
                m_pos = end + 1u;
                return token;
            }

            const auto start = m_pos;
            while (m_pos < m_text.size() && static_cast<unsigned char>(m_text[m_pos]) > ' ')
                m_pos++;

            return m_text.substr(start, m_pos - start);
        }

    private:
        void SkipWhitespaceAndComments()
        {
            while (m_pos < m_text.size())
            {
                if (m_text[m_pos] != '\0' && static_cast<unsigned char>(m_text[m_pos]) <= ' ')
                    m_pos++;
                else if (m_text.compare(m_pos, 2u, "//") == 0)
                {
                    const auto end = m_text.find('\n', m_pos);
                    m_pos = end == std::string::npos ? m_text.size() : end;
                }
                else
                    break;
            }
        }

        const std::string& m_text;
        size_t m_pos;
    };

    const char* SkipSpaces(const char* text, const char* end)
    {
        while (text < end && (*text == ' ' || *text == '\t'))
            text++;

        return text;
    }
} // namespace

namespace T5
{
    void BspEntity::SetValue(const std::string& key, std::string value)
    {
        for (auto& [existingKey, existingValue] : m_values)
        {
            if (utils::StringEqualsIgnoreCase(existingKey, key))
            {
                existingValue = std::move(value);
                return;
            }
        }

        m_values.emplace_back(key, std::move(value));
    }

    const std::string* BspEntity::GetValue(const std::string& key) const
    {
        const std::string* result = nullptr;
        for (const auto& [existingKey, existingValue] : m_values)
        {
            if (utils::StringEqualsIgnoreCase(existingKey, key))
                result = &existingValue;
        }

        return result;
    }

    std::string BspEntity::GetString(const std::string& key) const
    {
        const auto* value = GetValue(key);
        return value ? *value : std::string();
    }

    float BspEntity::GetFloat(const std::string& key, const float defaultValue) const
    {
        const auto* text = GetValue(key);
        auto value = defaultValue;
        if (text && !ParseBspFloat(*text, value))
            return 0.0f;

        return value;
    }

    int BspEntity::GetInt(const std::string& key, const int defaultValue) const
    {
        const auto* text = GetValue(key);
        if (!text)
            return defaultValue;

        return std::atoi(text->c_str());
    }

    bool BspEntity::GetVec3(const std::string& key, float* vec) const
    {
        const auto* text = GetValue(key);
        return text && ParseBspVec3(*text, vec);
    }

    const std::vector<std::pair<std::string, std::string>>& BspEntity::Values() const
    {
        return m_values;
    }

    std::optional<std::vector<BspEntity>> ParseBspEntities(const std::string& text)
    {
        std::vector<BspEntity> entities;
        EntityTokenizer tokenizer(text);

        while (const auto open = tokenizer.Next())
        {
            if (*open != "{")
                return std::nullopt;

            auto& entity = entities.emplace_back();
            while (true)
            {
                const auto key = tokenizer.Next();
                if (!key)
                    return std::nullopt;
                if (*key == "}")
                    break;

                auto value = tokenizer.Next();
                if (!value)
                    return std::nullopt;

                entity.SetValue(*key, std::move(*value));
            }
        }

        return entities;
    }

    std::string WriteBspEntities(const std::vector<BspEntity>& entities)
    {
        std::string text;
        for (const auto& entity : entities)
        {
            text += "{\n";
            for (const auto& [key, value] : entity.Values())
                text += std::format("\"{}\" \"{}\"\n", key, value);
            text += "}\n";
        }

        return text;
    }

    std::string FormatBspFloat(const float value)
    {
        return std::format("{}", value);
    }

    std::string FormatBspVec3(const float* vec)
    {
        return std::format("{} {} {}", vec[0], vec[1], vec[2]);
    }

    bool ParseBspFloat(const std::string& text, float& value)
    {
        const auto* end = text.data() + text.size();
        const auto* start = SkipSpaces(text.data(), end);
        if (start < end && *start == '+')
            start++;

        return std::from_chars(start, end, value).ec == std::errc();
    }

    bool ParseBspVec3(const std::string& text, float* vec)
    {
        const auto* end = text.data() + text.size();
        const auto* current = text.data();
        for (auto i = 0; i < 3; i++)
        {
            current = SkipSpaces(current, end);
            const auto [next, error] = std::from_chars(current, end, vec[i]);
            if (error != std::errc())
                return false;
            current = next;
        }

        return true;
    }
} // namespace T5

#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace T5
{
    class BspEntity
    {
    public:
        void SetValue(const std::string& key, std::string value);
        [[nodiscard]] const std::string* GetValue(const std::string& key) const;
        [[nodiscard]] std::string GetString(const std::string& key) const;
        [[nodiscard]] float GetFloat(const std::string& key, float defaultValue = 0.0f) const;
        [[nodiscard]] int GetInt(const std::string& key, int defaultValue = 0) const;
        [[nodiscard]] bool GetVec3(const std::string& key, float* vec) const;
        [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& Values() const;

    private:
        std::vector<std::pair<std::string, std::string>> m_values;
    };

    [[nodiscard]] std::optional<std::vector<BspEntity>> ParseBspEntities(const std::string& text);
    [[nodiscard]] std::string WriteBspEntities(const std::vector<BspEntity>& entities);

    [[nodiscard]] std::string FormatBspFloat(float value);
    [[nodiscard]] std::string FormatBspVec3(const float* vec);
    [[nodiscard]] bool ParseBspFloat(const std::string& text, float& value);
    [[nodiscard]] bool ParseBspVec3(const std::string& text, float* vec);
} // namespace T5

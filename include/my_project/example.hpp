#pragma once

/**
 * @brief Small example API for validating the template workflow.
 */
namespace my_project {

/**
 * @brief Add two integers.
 *
 * @param lhs Left-hand operand.
 * @param rhs Right-hand operand.
 * @return The sum of @p lhs and @p rhs.
 */
[[nodiscard]] constexpr auto add(const int lhs, const int rhs) noexcept -> int { return lhs + rhs; }

} // namespace my_project

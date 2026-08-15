/**
 * @file impulse_implog.hpp
 * @brief Canonical C++20 ImpLog (.implog) Datalog Rule Compiler & Fixpoint Engine for Impulse Graph.
 *
 * Provides declarative Datalog rule compilation, StratificationValidator (negation cycle detection),
 * MagicSetsTransformation, and bytecode generation for relationship-based access control (ReBAC)
 * and recursive transitive closure queries.
 */

#ifndef IMPULSE_IMPLOG_HPP
#define IMPULSE_IMPLOG_HPP

#include "impulse_datalog.hpp"

namespace impulse::implog {
    using namespace impulse::datalog;
}

#endif // IMPULSE_IMPLOG_HPP

#pragma once

#include "ImplicitParam.h"
#include "GenerationParams.h"
#include "Traits.h"
#include "rapidcheck/Generator.h"

namespace rc {
namespace detail {

template<typename T>
T RoseNode::pick(gen::GeneratorUP<T> &&generator)
{
    auto i = m_nextChild;
    if (m_nextChild >= m_children.size())
        m_children.emplace_back(this);
    m_nextChild++;

    auto &child = m_children[i];
    // TODO we should probably not set this every time
    child.setGenerator(std::move(generator));
    if (i == (m_shrinkChildren - 1)) {
        bool didShrink;
        T value(child.nextShrink<T>(didShrink));
        if (!didShrink)
            m_shrinkChildren = i + 2;
        return std::move(value);
    } else {
        return child.currentValue<T>();
    }
}

template<typename T>
void RoseNode::setGenerator(gen::GeneratorUP<T> &&generator)
{ m_canonicalGenerator = std::move(generator); }

template<typename T>
T RoseNode::currentValue()
{
    ImplicitParam<param::CurrentNode> currentNode;
    currentNode.let(this);
    m_nextChild = 0;
    return generatorCast<T>(currentGenerator())->generate();
}

template<typename T>
T RoseNode::nextShrink(bool &didShrink)
{ return nextShrink<T>(didShrink, IsCopyConstructible<T>()); }

// For copy-constructible types
template<typename T>
T RoseNode::nextShrink(bool &didShrink, std::true_type)
{
    // If shrinking is disabled, we can just return the current value.
    ImplicitParam<param::NoShrink> noShrink;
    if (*noShrink) {
        didShrink = false;
        return currentValue<T>();
    }

    if (!m_shrinkIterator) {
        // If we don't have a shrink iterator, shrink the children first.
        T value(nextShrinkChildren<T>(didShrink));
        // If any child did shrink, then we shouldn't shrink ourselves just yet.
        if (didShrink)
            return std::move(value);

        // Otherwise, we should make a shrink iterator
        m_acceptedGenerator = gen::GeneratorUP<T>(new gen::Constant<T>(value));
        // Always use the canonical generator to make a shrink iterator
        auto typedCanonical = generatorCast<T>(m_canonicalGenerator.get());
        m_shrinkIterator = shrink::UntypedIteratorUP(
            typedCanonical->shrink(std::move(value)));

        // Since the value of every child is now fixed, the number of
        // children won't change so we can get rid of any unused ones.
        m_children.resize(m_nextChild);
    }

    auto typedIterator = iteratorCast<T>(m_shrinkIterator);
    if (typedIterator->hasNext()) {
        // Current iterator still has more shrinks, use that.
        didShrink = true;
        auto gen = new gen::Constant<T>(typedIterator->next());
        m_currentGenerator = gen::GeneratorUP<T>(gen);
        return gen->generate();
    } else {
        // Exhausted
        didShrink = false;
        // Replace with shrink::nothing since that is likely smaller than the
        // existing.
        m_shrinkIterator = shrink::nothing<T>();
        m_currentGenerator = nullptr;
        return currentValue<T>();
    }
}

// For non-copy-constructible types.
template<typename T>
T RoseNode::nextShrink(bool &didShrink, std::false_type)
{
    // Only shrink children, cannot explicitly shrink non-copy-constructible
    // types.
    return nextShrinkChildren<T>(didShrink);
}

// Returns the next shrink by shrinking the children or the currently accepted
// if there are no more possible shrinks of the children. `didShrink` is set to
// true if any of them were shrunk.
template<typename T>
T RoseNode::nextShrinkChildren(bool &didShrink)
{
    // Set this to at least 1 to enable shrinking of children.
    m_shrinkChildren = std::max<std::size_t>(1, m_shrinkChildren);
    T value(currentValue<T>());
    didShrink = m_shrinkChildren <= m_nextChild;
    return std::move(value);
}

template<typename T>
gen::Generator<T> *RoseNode::generatorCast(gen::UntypedGenerator *gen)
{
    gen::Generator<T> *typed = dynamic_cast<gen::Generator<T> *>(gen);
    if (typed == nullptr)
        throw UnexpectedType(typeid(T), gen->generatedTypeInfo());

    return typed;
}

template<typename T>
shrink::Iterator<T> *RoseNode::iteratorCast(
    const shrink::UntypedIteratorUP &it)
{
    shrink::Iterator<T> *typed = dynamic_cast<shrink::Iterator<T> *>(it.get());
    // TODO use typeid of ShrunkTypes, not iterators themselves
    if (typed == nullptr)
        throw UnexpectedType(typeid(shrink::Iterator<T>), typeid(it));
    return typed;
}

} // namespace detail
} // namespace rc
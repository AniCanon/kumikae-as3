package app.anicanon.kumikae.as3.language

import com.intellij.codeInsight.editorActions.SimpleTokenSetQuoteHandler
import com.intellij.openapi.editor.highlighter.HighlighterIterator

/** TextMate may split a string into quote, escape and content runs; pairing uses its whole AS3 token. */
class As3QuoteHandler : SimpleTokenSetQuoteHandler(As3Tokens.STRING) {
    override fun isOpeningQuote(iterator: HighlighterIterator, offset: Int): Boolean =
        iterator is As3HighlightIterator && iterator.tokenType == As3Tokens.STRING && offset == iterator.structuralStart

    override fun isClosingQuote(iterator: HighlighterIterator, offset: Int): Boolean =
        iterator is As3HighlightIterator && iterator.tokenType == As3Tokens.STRING &&
            offset > iterator.structuralStart && offset == iterator.structuralEnd - 1

    override fun isNonClosedLiteral(iterator: HighlighterIterator, chars: CharSequence): Boolean {
        if (iterator !is As3HighlightIterator) return super.isNonClosedLiteral(iterator, chars)
        val start = iterator.structuralStart
        val end = iterator.structuralEnd
        return end - start < 2 || chars[start] != chars[end - 1]
    }
}

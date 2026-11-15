package app.anicanon.kumikae.as3.language

import app.anicanon.kumikae.as3.PluginFiles
import com.intellij.extapi.psi.ASTWrapperPsiElement
import com.intellij.extapi.psi.PsiFileBase
import com.intellij.lang.ASTNode
import com.intellij.lang.BracePair
import com.intellij.lang.Commenter
import com.intellij.lang.Language
import com.intellij.lang.PairedBraceMatcher
import com.intellij.lang.ParserDefinition
import com.intellij.lang.PsiParser
import com.intellij.lexer.Lexer
import com.intellij.openapi.fileTypes.LanguageFileType
import com.intellij.openapi.project.Project
import com.intellij.psi.FileViewProvider
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType
import com.intellij.psi.tree.IFileElementType
import com.intellij.psi.tree.TokenSet
import javax.swing.Icon

/**
 * ActionScript 3 as a language of the IDE's own. Without this a script is plain text with colours, and
 * the IDE does not ask the language server where a name is declared. The id is the plugin's own, so
 * that it never meets another plugin's ActionScript.
 */
object As3Language : Language("KumikaeAS3") {
    private fun readResolve(): Any = As3Language

    override fun getDisplayName() = "ActionScript 3"
}

class As3FileType private constructor() : LanguageFileType(As3Language) {
    override fun getName() = "ActionScript 3"

    override fun getDescription() = "ActionScript 3 script for Godot"

    override fun getDefaultExtension() = "as"

    override fun getIcon(): Icon = PluginFiles.script

    companion object {
        @JvmField
        val INSTANCE = As3FileType()
    }
}

class As3File(provider: FileViewProvider) : PsiFileBase(provider, As3Language) {
    override fun getFileType() = As3FileType.INSTANCE
}

class As3ParserDefinition : ParserDefinition {
    override fun createLexer(project: Project?): Lexer = As3Lexer()

    // The file is its tokens, one after another: the IDE needs the pieces, not a tree.
    override fun createParser(project: Project?) = PsiParser { root, builder ->
        val whole = builder.mark()
        while (!builder.eof()) builder.advanceLexer()
        whole.done(root)
        builder.treeBuilt
    }

    override fun getFileNodeType() = FILE

    override fun getCommentTokens() = COMMENTS

    override fun getStringLiteralElements() = STRINGS

    override fun createElement(node: ASTNode): PsiElement = ASTWrapperPsiElement(node)

    override fun createFile(viewProvider: FileViewProvider): PsiFile = As3File(viewProvider)

    private companion object {
        val FILE = IFileElementType(As3Language)
        val COMMENTS = TokenSet.create(As3Tokens.LINE_COMMENT, As3Tokens.BLOCK_COMMENT)
        val STRINGS = TokenSet.create(As3Tokens.STRING)
    }
}

class As3Commenter : Commenter {
    override fun getLineCommentPrefix() = "//"

    override fun getBlockCommentPrefix() = "/*"

    override fun getBlockCommentSuffix() = "*/"

    override fun getCommentedBlockCommentPrefix(): String? = null

    override fun getCommentedBlockCommentSuffix(): String? = null
}

class As3BraceMatcher : PairedBraceMatcher {
    override fun getPairs() = PAIRS

    override fun isPairedBracesAllowedBeforeType(lbraceType: IElementType, contextType: IElementType?) =
        contextType == null || contextType == TokenType.WHITE_SPACE || contextType == As3Tokens.SYMBOL ||
            contextType == As3Tokens.RPAREN || contextType == As3Tokens.RBRACKET || contextType == As3Tokens.RBRACE

    override fun getCodeConstructStart(file: PsiFile?, openingBraceOffset: Int) = openingBraceOffset

    private companion object {
        val PAIRS = arrayOf(
            BracePair(As3Tokens.LBRACE, As3Tokens.RBRACE, true),
            BracePair(As3Tokens.LPAREN, As3Tokens.RPAREN, false),
            BracePair(As3Tokens.LBRACKET, As3Tokens.RBRACKET, false),
        )
    }
}

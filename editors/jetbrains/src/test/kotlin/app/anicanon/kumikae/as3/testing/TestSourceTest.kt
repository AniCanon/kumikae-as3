package app.anicanon.kumikae.as3.testing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TestSourceTest {
    private val text = """
        package game {
            // public function fake():void {}
            public class CourseTest {
                private var label:String = "function fake()";
                [Test] public function gapJump():void { check(true); }
                public function helper():void { }
                [Test] public function magnetCollects():void { }
            }
        }
    """.trimIndent()

    @Test fun discoversOnlyAnnotatedMethodsWithinTheConfiguredFolder() {
        val manifest = """{"version":1,"tests":[{"name":"Models","discover":"tests"}]}"""
        val source = TestSource.read(text)
        assertEquals(
            listOf("Models::game.CourseTest::gapJump", "Models::game.CourseTest::magnetCollects"),
            TestCaseMappings.forFile(manifest, "tests/CourseTest.as", source).map { it.identity },
        )
        assertEquals(emptyList<TestSelection>(), TestCaseMappings.forFile(manifest, "src/CourseTest.as", source))
    }

    @Test fun selectsOnlyTheActualMethodOrClass() {
        val source = TestSource.read(text)
        assertEquals(listOf("gapJump", "helper", "magnetCollects"), source.methods.map { it.name })
        assertEquals("gapJump", source.methodAt(text.indexOf("check(true)"))?.name)
        assertNull(source.methodAt(text.indexOf("class CourseTest")))
        assertEquals("CourseTest", source.className)
    }

    @Test fun commentsStringsAndNestedFunctionsDoNotCreateCases() {
        val source = TestSource.read("class T { function real():void { function nested():void {} } }")
        assertEquals(listOf("real"), source.methods.map { it.name })
    }

    @Test fun defaultObjectDoesNotEndTheMethod() {
        val text = "class T { function test(options:Object = {flag:true}):void { check(true); } }"
        assertEquals("test", TestSource.read(text).methodAt(text.indexOf("check(true)"))?.name)
    }

    @Test fun failureFramesKeepPathsAndOneBasedLines() {
        val frames = As3FailureFrames.read(
            "Error: fail\n\tat Assert/fail()[/lib/kumikae/testing/Assert.as:5]\n" +
                "\tat T/check()[/project with space/T.as:24]\n\tat T/run()[/project with space/T.as:10]",
        )
        assertEquals(3, frames.size)
        assertEquals("/project with space/T.as", frames.first { !it.framework }.path)
        assertEquals(24, frames.first { !it.framework }.line)
        assertEquals(emptyList<As3FailureFrame>(), As3FailureFrames.read("Error: [not a stack:4]"))
    }

    @Test fun accessorBodiesAreHelperContexts() {
        val text = "class T { function get helper():int { return 1; } function set helper(v:int):void { check(v); } }"
        val source = TestSource.read(text)
        assertEquals("get helper", source.methodAt(text.indexOf("return 1"))?.name)
        assertEquals("set helper", source.methodAt(text.indexOf("check(v)"))?.name)
    }
}

#!/usr/bin/env python3
"""Writes one AS3 class with one very long method, to measure how lowering grows with a function's size.

    python3 generate_function.py <shape> <statements> [depth] <F.as>

The class is `F`, so the file must be named F.as. Each shape stresses one thing the lowerer tracks for a
whole function: the temporaries of its statements, and the loops and `try` ranges around them.

  plain         the statements alone
  arguments     one call with that many arguments, each a call
  chosen_arguments  one call with that many arguments, each a choice between two values
  conditionals  each statement chooses between two values, inside `depth` nested for-each loops
  catch         the statements in one try/catch
  finally       the statements in one try/finally
  nested_try    the statements inside `depth` nested try/catch
  nested_finally  the statements and a return inside `depth` nested try/finally
  loops         one try/catch of the statements, inside `depth` nested for-each loops
  plain_loops   the statements inside `depth` nested for-each loops
  returns       a return after every tenth statement
  returns_finally  the same, in one try/finally
  tries         the statements in try/catch blocks of `depth` statements, one after another
"""
import sys


def statements(count, indent):
    return [indent + ("label = label + k + \":\" + n;" if index % 2 == 0 else "items.push(make(k));") for index in range(count)]


def loops(depth):
    return ["\t\tfor each (var x%d:Object in items) {" % index for index in range(depth)]


def with_returns(count):
    body = []
    for index, line in enumerate(statements(count, "\t\t\t")):
        body.append(line)
        if index % 10 == 9:
            body.append("\t\t\tif (k == %d) return make(%d);" % (index, index))
    return body


def body_of(shape, count, depth):
    inner = statements(count, "\t\t\t")
    caught = ["\t\ttry {"] + inner + ["\t\t} catch (e:Error) {", "\t\t\tn++;", "\t\t}"]
    if shape == "plain":
        return inner
    if shape == "arguments":
        return ["\t\tn += many(" + ", ".join("make(%d)" % index for index in range(count)) + ");"]
    if shape == "chosen_arguments":
        return ["\t\tn += many(" + ", ".join("k > %d ? make(k) : label" % index for index in range(count)) + ");"]
    if shape == "conditionals":
        chosen = ["\t\t\titems.push(k > %d ? make(k) : label);" % index for index in range(count)]
        return loops(depth) + chosen + ["\t\t}"] * depth
    if shape == "catch":
        return caught
    if shape == "finally":
        return ["\t\ttry {"] + inner + ["\t\t} finally {", "\t\t\tn++;", "\t\t}"]
    if shape == "nested_try":
        return ["\t\ttry {"] * depth + inner + ["\t\t} catch (e%d:Error) { n++; }" % index for index in range(depth)]
    if shape == "nested_finally":
        return ["\t\ttry {"] * depth + inner + ["\t\t\treturn;"] + ["\t\t} finally { n++; }"] * depth
    if shape == "loops":
        return loops(depth) + caught + ["\t\t}"] * depth
    if shape == "plain_loops":
        return loops(depth) + inner + ["\t\t}"] * depth
    if shape == "returns":
        return with_returns(count) + ["\t\treturn null;"]
    if shape == "returns_finally":
        return ["\t\ttry {"] + with_returns(count) + ["\t\t} finally {", "\t\t\tn++;", "\t\t}", "\t\treturn null;"]
    if shape == "tries":
        body = []
        for start in range(0, count, depth):
            body += ["\t\ttry {"] + statements(min(depth, count - start), "\t\t\t") + ["\t\t} catch (e%d:Error) { n++; }" % start]
        return body
    sys.exit("unknown shape %s\n%s" % (shape, __doc__))


def function_text(shape, count, depth=1):
    result = "Object" if shape.startswith("returns") else "void"
    lines = ["package {", "public class F {", "\tprivate var label:String = \"a\";", "\tprivate var items:Array = [];",
             "\tprivate var n:int = 0;", "\tpublic function make(i:int):Object { return items; }",
             "\tpublic function many(...rest):int { return 1; }", "\tpublic function big(k:int):%s {" % result]
    return "\n".join(lines + body_of(shape, count, depth) + ["\t}", "}", "}", ""])


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    depth = int(sys.argv[3]) if len(sys.argv) == 5 else 1
    with open(sys.argv[-1], "w", encoding="utf-8") as output:
        output.write(function_text(sys.argv[1], int(sys.argv[2]), depth))


if __name__ == "__main__":
    main()

// Standard library tests: Xml.

#include "common/resource_limits.hpp"
#include "stdlib_test_helpers.hpp"

static void test_xml_add_child() {
    const auto v = eval("Xml.child_count(Xml.add_child("
                        "Result.unwrap(Xml.deserialize(\"<root/>\")), "
                        "Xml.element(\"child\")))");

    ASSERT_EQ(v.as_integer(), 1);
}

static void test_xml_attribute() {
    ASSERT_EVAL_STR("Xml.attribute(Result.unwrap(Xml.deserialize("
                    "\"<root id=\\\"42\\\"/>\")), \"id\")",
                    "42");
}

static void test_xml_child_count() {
    const auto v =
        eval("Xml.child_count(Result.unwrap(Xml.deserialize(\"<root><a/><b/></root>\")))");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_xml_children() {
    const auto v = eval("Xml.children(Result.unwrap(Xml.deserialize(\"<root><a/><b/></root>\")))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
}

static void test_xml_element() {
    const auto result = eval("Xml.element(\"test\") |> Xml.tag()");

    ASSERT_EQ(result.as_string(), "test");
}

static void test_xml_has_attribute() {
    const auto t = eval("Xml.has_attribute(Result.unwrap(Xml.deserialize("
                        "\"<root id=\\\"42\\\"/>\")), \"id\")");

    ASSERT_TRUE(t.is_bool() && t.as_bool());
}

static void test_xml_is_leaf() {
    const auto t = eval("Xml.is_leaf(Result.unwrap(Xml.deserialize(\"<leaf/>\")))");

    ASSERT_TRUE(t.is_bool() && t.as_bool());

    const auto f = eval("Xml.is_leaf(Result.unwrap(Xml.deserialize(\"<root><a/></root>\")))");

    ASSERT_TRUE(f.is_bool() && !f.as_bool());
}

static void test_xml_is_valid() {
    const auto result = eval(R"(
        Xml.is_valid("<root/>")
    )");

    ASSERT_TRUE(result.is_truthy());
}

static void test_xml_is_valid_false() {
    const auto result = eval(R"(
        Xml.is_valid("<root>")
    )");

    ASSERT_FALSE(result.is_truthy());
}

static void test_xml_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Xml.deserialize"));
    ASSERT_TRUE(env->has("Xml.deserialize_detailed"));
    ASSERT_TRUE(env->has("Xml.element"));
    ASSERT_TRUE(env->has("Xml.serialize"));
    ASSERT_TRUE(env->has("Xml.tag"));
    ASSERT_TRUE(env->has("Xml.text"));
    ASSERT_TRUE(env->has("Xml.find"));
    ASSERT_TRUE(env->has("Xml.children"));
}

static void test_xml_parse_serialize() {
    auto result = eval(
        "Xml.deserialize(\"<root><item>hello</item></root>\") |> Result.unwrap() |> Xml.tag()");

    ASSERT_EQ(result.as_string(), "root");
}

static void test_xml_serialize_pretty() {
    const auto v =
        eval("Xml.serialize_pretty(Result.unwrap(Xml.deserialize(\"<root><a/></root>\")))");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find('\n') != std::string::npos);
}

static void test_xml_set_attribute() {
    const auto result = eval("Xml.element(\"div\") |> Xml.set_attribute(\"class\", \"main\") |> "
                             "Xml.attribute(\"class\") |> Result.unwrap()");

    ASSERT_EQ(result.as_string(), "main");
}

static void test_xml_set_tag() {
    const auto v = eval("Xml.tag(Xml.set_tag(Xml.element(\"old\"), \"new\"))");

    ASSERT_EQ(v.as_string(), "new");
}

static void test_xml_set_text() {
    ASSERT_EVAL_STR("Xml.text(Xml.set_text(Xml.element(\"msg\"), \"hello\"))", "hello");
}

static void test_xml_tag() {
    const auto v = eval("Xml.tag(Result.unwrap(Xml.deserialize(\"<root/>\")))");

    ASSERT_TRUE(v.is_string());
    ASSERT_EQ(v.as_string(), "root");
}

static void test_xml_text() {
    ASSERT_EVAL_STR("Xml.text(Result.unwrap(Xml.deserialize(\"<root>hello</root>\")))", "hello");
}

// ─── Additional positive coverage ────────────────────────────────────────

static void test_xml_remove_attribute() {
    const auto v = eval("Xml.has_attribute(Xml.remove_attribute(Xml.set_attribute("
                        "Xml.element(\"div\"), \"id\", \"x\"), \"id\"), \"id\")");

    ASSERT_TRUE(v.is_bool() && !v.as_bool());
}

static void test_xml_attributes_dict() {
    const auto v = eval("Xml.attributes(Result.unwrap(Xml.deserialize("
                        "\"<a x=\\\"1\\\" y=\\\"2\\\"/>\")))");

    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->entries.size(), 2U);

    const auto* x = v.as_dictionary()->find("x");

    ASSERT_TRUE(x != nullptr);
    ASSERT_EQ(x->as_string(), "1");
}

static void test_xml_set_cdata_serialize() {
    const auto v = eval("Xml.serialize(Xml.set_cdata(Xml.element(\"s\"), \"a<b\"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("<![CDATA[a<b]]>") != std::string::npos);
}

static void test_xml_add_comment_serialize() {
    const auto v = eval("Xml.serialize(Xml.add_comment(Xml.element(\"d\"), \"hi\"))");

    ASSERT_TRUE(v.is_string());
    ASSERT_TRUE(v.as_string().find("<!--hi-->") != std::string::npos);
}

static void test_xml_to_dictionary() {
    const auto v = eval("Xml.to_dictionary(Result.unwrap(Xml.deserialize("
                        "\"<r><a>1</a><b>2</b></r>\")))");

    ASSERT_TRUE(v.is_dictionary());
    ASSERT_EQ(v.as_dictionary()->entries.size(), 2U);

    const auto* a = v.as_dictionary()->find("a");

    ASSERT_TRUE(a != nullptr);
    ASSERT_EQ(a->as_string(), "1");
}

static void test_xml_from_dictionary() {
    const auto v = eval("Xml.tag(Xml.from_dictionary(\"item\", {\"k\": \"v\"}))");

    ASSERT_EQ(v.as_string(), "item");

    const auto count = eval("Xml.child_count(Xml.from_dictionary(\"item\", {\"k\": \"v\"}))");

    ASSERT_EQ(count.as_integer(), 1);
}

static void test_xml_children_by_tag() {
    const auto v = eval("Xml.children_by_tag(Result.unwrap(Xml.deserialize("
                        "\"<r><a/><b/><a/></r>\")), \"a\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
}

static void test_xml_has_child() {
    const auto t =
        eval("Xml.has_child(Result.unwrap(Xml.deserialize(\"<r><item/></r>\")), \"item\")");

    ASSERT_TRUE(t.is_bool() && t.as_bool());

    const auto f = eval("Xml.has_child(Result.unwrap(Xml.deserialize(\"<r><item/></r>\")), \"x\")");

    ASSERT_TRUE(f.is_bool() && !f.as_bool());
}

static void test_xml_find() {
    const auto v = eval("Xml.tag(Result.unwrap(Xml.find(Result.unwrap(Xml.deserialize("
                        "\"<r><item id=\\\"1\\\"/></r>\")), \"item\")))");

    ASSERT_EQ(v.as_string(), "item");
}

static void test_xml_find_all() {
    const auto v = eval("Xml.find_all(Result.unwrap(Xml.deserialize("
                        "\"<r><item/><item/><other/></r>\")), \"item\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 2U);
}

static void test_xml_find_by_attribute() {
    ASSERT_EVAL_STR("Xml.attribute(Result.unwrap(Xml.find_by_attribute(Result.unwrap("
                    "Xml.deserialize(\"<r><item id=\\\"42\\\"/><item id=\\\"99\\\"/></r>\")), "
                    "\"id\", \"42\")), \"id\")",
                    "42");
}

static void test_xml_at() {
    const auto v = eval("Xml.tag(Result.unwrap(Xml.at(Result.unwrap(Xml.deserialize("
                        "\"<root><child><leaf/></child></root>\")), \"root/child/leaf\")))");

    ASSERT_EQ(v.as_string(), "leaf");
}

static void test_xml_text_at() {
    ASSERT_EVAL_STR("Xml.text_at(Result.unwrap(Xml.deserialize("
                    "\"<root><greeting>Hello</greeting></root>\")), \"root/greeting\")",
                    "Hello");
}

static void test_xml_find_descendant() {
    const auto v =
        eval("Xml.tag(Result.unwrap(Xml.find_descendant(Result.unwrap(Xml.deserialize("
             "\"<r><a><deep tag=\\\"1\\\"/></a></r>\")), \"deep\")))");

    ASSERT_EQ(v.as_string(), "deep");
}

static void test_xml_find_descendant_not_found() {
    ASSERT_EVAL_FAILURE(
        "Xml.find_descendant(Result.unwrap(Xml.deserialize(\"<r><a/></r>\")), \"missing\")");
}

static void test_xml_find_all_descendants() {
    const auto v = eval("Xml.find_all_descendants(Result.unwrap(Xml.deserialize("
                        "\"<r><item/><a><item/></a><item/></r>\")), \"item\")");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
}

static void test_xml_get_path() {
    const auto v =
        eval("Result.unwrap(Xml.text(Result.unwrap(Xml.get_path(Result.unwrap(Xml.deserialize("
             "\"<lib><book><title>A</title></book></lib>\")), \"book/title\")))))");

    ASSERT_EQ(v.as_string(), "A");
}

static void test_xml_get_path_indexed() {
    const auto v =
        eval("Result.unwrap(Xml.text(Result.unwrap(Xml.get_path(Result.unwrap(Xml.deserialize("
             "\"<lib><book><title>A</title></book>"
             "<book><title>B</title></book></lib>\")), \"book[1]/title\")))))");

    ASSERT_EQ(v.as_string(), "B");
}

static void test_xml_get_path_missing() {
    ASSERT_EVAL_FAILURE(
        "Xml.get_path(Result.unwrap(Xml.deserialize(\"<r><a/></r>\")), \"a/missing\")");
}

static void test_xml_remove_child() {
    const auto v = eval("Xml.child_count(Result.unwrap(Xml.remove_child(Result.unwrap("
                        "Xml.deserialize(\"<r><a/><b/><c/></r>\")), 1)))");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_xml_remove_child_removes_correct() {
    // Removing element index 0 leaves the serialised <b/> as the first child.
    const auto v = eval("Xml.serialize(Result.unwrap(Xml.remove_child(Result.unwrap("
                        "Xml.deserialize(\"<r><a/><b/></r>\")), 0)))");

    ASSERT_EQ(v.as_string(), "<r><b/></r>");
}

static void test_xml_remove_child_out_of_bounds() {
    ASSERT_EVAL_FAILURE(
        "Xml.remove_child(Result.unwrap(Xml.deserialize(\"<r><a/></r>\")), 5)");
    ASSERT_EVAL_FAILURE(
        "Xml.remove_child(Result.unwrap(Xml.deserialize(\"<r><a/></r>\")), -1)");
}

static void test_xml_replace_child() {
    const auto v = eval("Xml.serialize(Result.unwrap(Xml.replace_child(Result.unwrap("
                        "Xml.deserialize(\"<r><a/><b/></r>\")), 0, Xml.element(\"z\"))))");

    ASSERT_EQ(v.as_string(), "<r><z/><b/></r>");
}

static void test_xml_replace_child_out_of_bounds() {
    ASSERT_EVAL_FAILURE("Xml.replace_child(Result.unwrap(Xml.deserialize(\"<r><a/></r>\")), "
                        "3, Xml.element(\"z\"))");
}

static void test_xml_inner_text() {
    // inner_text concatenates every descendant text node, unlike Xml.text which
    // only reads the node's own direct text children.
    const auto v = eval("Xml.inner_text(Result.unwrap(Xml.deserialize("
                        "\"<r>a<b>b</b>c<d>d</d></r>\")))");

    ASSERT_EQ(v.as_string(), "abcd");
}

static void test_xml_escape() {
    const auto v = eval("Xml.escape(\"a<b>&\\\"'c\")");

    ASSERT_EQ(v.as_string(), "a&lt;b&gt;&amp;&quot;&apos;c");
}

static void test_xml_unescape() {
    ASSERT_EVAL_STR("Xml.unescape(\"a&lt;b&gt;&amp;&quot;&apos;c\")", "a<b>&\"'c");
}

static void test_xml_escape_unescape_roundtrip() {
    ASSERT_EVAL_STR("Xml.unescape(Xml.escape(\"<tag attr=\\\"v\\\" & 'x'>\"))",
                    "<tag attr=\"v\" & 'x'>");
}

static void test_xml_unescape_numeric() {
    ASSERT_EVAL_STR("Xml.unescape(\"&#65;&#x42;\")", "AB");
}

static void test_xml_unescape_malformed() {
    ASSERT_EVAL_FAILURE("Xml.unescape(\"&foo;\")");
    ASSERT_EVAL_FAILURE("Xml.unescape(\"&amp\")");
}

static void test_xml_serialize_compact() {
    const auto v = eval("Xml.serialize(Result.unwrap(Xml.deserialize(\"<r><a>x</a></r>\")))");

    ASSERT_EQ(v.as_string(), "<r><a>x</a></r>");
}

static void test_xml_entity_decode() {
    ASSERT_EVAL_STR("Xml.text(Result.unwrap(Xml.deserialize("
                    "\"<r>a&lt;b&amp;c&gt;d</r>\")))",
                    "a<b&c>d");
}

static void test_xml_serialize_escapes_text() {
    const auto v = eval("Xml.serialize(Xml.set_text(Xml.element(\"r\"), \"a<b&c\"))");

    ASSERT_EQ(v.as_string(), "<r>a&lt;b&amp;c</r>");
}

static void test_xml_cdata_roundtrip() {
    // Parser-produced CDATA never contains "]]>", so serialize re-wraps it cleanly.
    const auto v =
        eval("Xml.serialize(Result.unwrap(Xml.deserialize(\"<s><![CDATA[x<y&z]]></s>\")))");

    ASSERT_TRUE(v.as_string().find("<![CDATA[x<y&z]]>") != std::string::npos);
}

static void test_xml_comment_roundtrip() {
    const auto v = eval("Xml.serialize(Result.unwrap(Xml.deserialize(\"<d><!--note--></d>\")))");

    ASSERT_TRUE(v.as_string().find("<!--note-->") != std::string::npos);
}

static void test_xml_self_closing() {
    const auto leaf = eval("Xml.is_leaf(Result.unwrap(Xml.deserialize(\"<a/>\")))");

    ASSERT_TRUE(leaf.is_bool() && leaf.as_bool());

    const auto s = eval("Xml.serialize(Result.unwrap(Xml.deserialize(\"<a/>\")))");

    ASSERT_EQ(s.as_string(), "<a/>");
}

static void test_xml_declaration_skipped() {
    const auto v = eval("Xml.tag(Result.unwrap(Xml.deserialize("
                        "\"<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?><r/>\")))");

    ASSERT_EQ(v.as_string(), "r");
}

static void test_xml_attribute_entity_roundtrip() {
    // Attribute value with all five escapables survives serialize → parse.
    ASSERT_EVAL_STR("Xml.attribute(Result.unwrap(Xml.deserialize(Xml.serialize("
                    "Xml.set_attribute(Xml.element(\"n\"), \"k\", \"a<b>&\\\"'\")))), \"k\")",
                    "a<b>&\"'");
}

// ─── Negative and security coverage ──────────────────────────────────────

static void test_xml_attribute_not_found() {
    ASSERT_EVAL_FAILURE("Xml.attribute(Result.unwrap(Xml.deserialize(\"<r/>\")), \"missing\")");
}

static void test_xml_text_no_content() {
    ASSERT_EVAL_FAILURE("Xml.text(Xml.element(\"empty\"))");
}

static void test_xml_find_not_found() {
    ASSERT_EVAL_FAILURE("Xml.find(Result.unwrap(Xml.deserialize(\"<r/>\")), \"nope\")");
}

static void test_xml_find_by_attribute_not_found() {
    ASSERT_EVAL_FAILURE("Xml.find_by_attribute(Result.unwrap(Xml.deserialize("
                        "\"<r><item class=\\\"a\\\"/></r>\")), \"id\", \"99\")");
}

static void test_xml_at_missing_path() {
    ASSERT_EVAL_FAILURE("Xml.at(Result.unwrap(Xml.deserialize(\"<r/>\")), \"r/missing\")");
}

static void test_xml_text_at_missing_path() {
    ASSERT_EVAL_FAILURE("Xml.text_at(Result.unwrap(Xml.deserialize(\"<r/>\")), \"r/missing\")");
}

static void test_xml_deserialize_unclosed() {
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"<root>\")");
}

static void test_xml_deserialize_empty() {
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"\")");
}

static void test_xml_deserialize_unterminated_attributes() {
    // Regression: a start-tag whose attribute region ends in trailing whitespace
    // at EOF drove the attribute scanner to read one byte past the source view
    // (input_[pos_] with pos_ == size()).  These malformed inputs must be
    // rejected cleanly, and is_valid must report false, without reading OOB.
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"<a \")");
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"<a x='1' \")");

    const auto valid = eval("Xml.is_valid(\"<a \")");

    ASSERT_FALSE(valid.is_truthy());
}

static void test_xml_deserialize_mismatched_tag() {
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"<a></b>\")");
}

static void test_xml_deserialize_trailing_content() {
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"<a/>junk\")");
}

static void test_xml_deserialize_doctype_rejected() {
    // Security: DOCTYPE declarations are rejected (external-entity injection risk).
    ASSERT_EVAL_FAILURE("Xml.deserialize(\"<!DOCTYPE html><r/>\")");
}

static void test_xml_is_valid_doctype() {
    const auto v = eval("Xml.is_valid(\"<!DOCTYPE html><r/>\")");

    ASSERT_FALSE(v.is_truthy());
}

static void test_xml_deserialize_too_deep() {
    std::string deep;

    for (int i{0}; i < 200; ++i) {
        deep += "<a>";
    }

    for (int i{0}; i < 200; ++i) {
        deep += "</a>";
    }

    const auto valid = eval("Xml.is_valid(\"" + deep + "\")");

    ASSERT_FALSE(valid.is_truthy());

    ASSERT_EVAL_FAILURE("Xml.deserialize(\"" + deep + "\")");
}

static void test_xml_wrong_type_throws() {
    ASSERT_THROWS(eval("Xml.tag(42)"));
    ASSERT_THROWS(eval("Xml.serialize(\"not xml\")"));
}

static void test_xml_deserialize_file_missing() {
    ASSERT_EVAL_FAILURE("Xml.deserialize_file(\"nonexistent_luma_xml_xyz.xml\")");
}

static void test_xml_deserialize_file_rejects_oversized_file() {
    // deserialize_file slurps the whole file into memory before parsing.  A
    // file larger than the maximum string size must be rejected up front,
    // yielding a failure result rather than an unbounded allocation.  The
    // payload is *valid* XML larger than the lowered cap, so the size guard is
    // the only thing that can make deserialize_file fail here; a malformed
    // payload would let the test pass even with the guard removed, masking a
    // regression.  The file is created under the default cap, then the cap is
    // lowered so the test need not materialise a 256 MB file.
    const LumaTempFile file{"_test_xml_oversize.xml", "<root>hello world</root>"};
    const LimitGuard guard{ResourceLimits::max_string_size, static_cast<std::size_t>(16)};

    ASSERT_EVAL_FAILURE("Xml.deserialize_file(\"_test_xml_oversize.xml\")");
}

static void test_xml_invalid_names_rejected() {
    // Regression: element/attribute/tag names were emitted raw, so a name that
    // is not a valid XML name (a space, '<', empty, …) serialised to malformed,
    // unparseable markup and could inject structure.  Construction now rejects
    // any name outside the grammar the parser accepts, at every name-setting
    // entry point (element, set_tag, set_attribute, from_dictionary).
    ASSERT_THROWS(eval("Xml.element(\"bad tag\")"));
    ASSERT_THROWS(eval("Xml.element(\"a<b\")"));
    ASSERT_THROWS(eval("Xml.element(\"\")"));
    ASSERT_THROWS(eval("Xml.set_tag(Xml.element(\"ok\"), \"bad tag\")"));
    ASSERT_THROWS(eval("Xml.set_attribute(Xml.element(\"ok\"), \"bad name\", \"v\")"));
    ASSERT_THROWS(eval("Xml.from_dictionary(\"bad root\", {\"k\": \"v\"})"));
    ASSERT_THROWS(eval("Xml.from_dictionary(\"root\", {\"bad key\": \"v\"})"));
}

static void test_xml_valid_punctuation_names_round_trip() {
    // Names using the allowed XML name punctuation (':', '-', '.', '_') are
    // accepted and survive a serialize/parse round-trip unchanged — proving the
    // validator does not over-reject legitimate names.
    const auto v =
        eval("Xml.tag(Result.unwrap(Xml.deserialize(Xml.serialize("
             "Xml.set_attribute(Xml.element(\"ns:item-1.0_x\"), \"data-id\", \"7\")))))");

    ASSERT_EQ(v.as_string(), "ns:item-1.0_x");
}

// ── Xml.to_node: typed recursive Xml.Node ADT ──

static void test_xml_to_node_element() {
    // An element becomes Node.Element(tag, attributes, children).  to_node keeps
    // every child node (element, text, comment, CDATA), unlike Xml.children.
    const auto v = eval("Xml.to_node(Result.unwrap(Xml.deserialize("
                        "\"<root id=\\\"1\\\"><a>hi</a><!-- c --></root>\")))");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, "Node");
    ASSERT_EQ(v.as_choice()->variant, "Element");
    ASSERT_EQ(v.as_choice()->fields.size(), 3U);

    // field 0: tag (string)
    ASSERT_EQ(v.as_choice()->fields[0].as_string(), "root");

    // field 1: attributes (dictionary<string>)
    ASSERT_TRUE(v.as_choice()->fields[1].is_dictionary());
    ASSERT_EQ(v.as_choice()->fields[1].as_dictionary()->entries.size(), std::size_t{1});

    // field 2: children (array<Xml.Node>) — the <a> element plus the comment.
    ASSERT_TRUE(v.as_choice()->fields[2].is_array());
    ASSERT_EQ(v.as_choice()->fields[2].as_array()->elements->size(), std::size_t{2});
}

static void test_xml_to_node_child_variants() {
    // The children of the element carry their own variants: the nested <a>
    // element and the trailing comment.
    const auto v = eval("Xml.to_node(Result.unwrap(Xml.deserialize("
                        "\"<root><a>hi</a><!-- c --></root>\")))");

    const auto& kids = *v.as_choice()->fields[2].as_array()->elements;
    ASSERT_EQ(kids.size(), std::size_t{2});

    // First child: an Element whose own child is the text node "hi".
    ASSERT_EQ(kids.at(0).as_choice()->variant, "Element");
    ASSERT_EQ(kids.at(0).as_choice()->fields[0].as_string(), "a");
    const auto& grandkids = *kids.at(0).as_choice()->fields[2].as_array()->elements;
    ASSERT_EQ(grandkids.size(), std::size_t{1});
    ASSERT_EQ(grandkids.at(0).as_choice()->variant, "Text");
    ASSERT_EQ(grandkids.at(0).as_choice()->fields[0].as_string(), "hi");

    // Second child: a Comment carrying its raw content.
    ASSERT_EQ(kids.at(1).as_choice()->variant, "Comment");
    ASSERT_EQ(kids.at(1).as_choice()->fields[0].as_string(), " c ");
}

static void test_xml_to_node_cdata() {
    // A CDATA section round-trips to Node.CData(content).
    const auto v = eval("Xml.to_node(Result.unwrap(Xml.deserialize("
                        "\"<root><![CDATA[a<b]]></root>\")))");

    const auto& kids = *v.as_choice()->fields[2].as_array()->elements;
    ASSERT_EQ(kids.size(), std::size_t{1});
    ASSERT_EQ(kids.at(0).as_choice()->variant, "CData");
    ASSERT_EQ(kids.at(0).as_choice()->fields[0].as_string(), "a<b");
}

static void test_xml_to_node_empty_element() {
    // A leaf element has an empty children array, not a missing field.
    const auto v = eval("Xml.to_node(Result.unwrap(Xml.deserialize(\"<root/>\")))");

    ASSERT_EQ(v.as_choice()->variant, "Element");
    ASSERT_TRUE(v.as_choice()->fields[1].as_dictionary()->entries.empty());
    ASSERT_TRUE(v.as_choice()->fields[2].as_array()->elements->empty());
}

// ── Xml.deserialize_detailed: typed Xml.Node + located Xml.ParseError ──

static void test_xml_deserialize_detailed_success() {
    // On success the typed Xml.Node tree is returned directly (no separate
    // to_node step), mirroring Json.parse_detailed.
    const auto v = eval("Xml.deserialize_detailed(\"<root><a/></root>\") |> Result.unwrap()");

    ASSERT_TRUE(v.is_choice());
    ASSERT_EQ(v.as_choice()->type_name, "Node");
    ASSERT_EQ(v.as_choice()->variant, "Element");
    ASSERT_EQ(v.as_choice()->fields[0].as_string(), "root");
}

static void test_xml_deserialize_detailed_failure_message() {
    const auto v = eval(R"(Xml.deserialize_detailed("<root>"))");
    ASSERT_TRUE(v.is_result());
    ASSERT_FALSE(v.as_result()->is_success);

    const auto& err = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(err->type_name, std::string{"ParseError"});
    ASSERT_FALSE(err->find_field("message")->as_string().empty());
}

static void test_xml_deserialize_detailed_failure_location() {
    // The unclosed <a> on the second line leaves the parser reaching end of input;
    // the located error reports a 1-based line/column.
    const auto v = eval(R"(Xml.deserialize_detailed("<root>\n  <a>\n  bad"))");
    ASSERT_TRUE(v.is_result());
    ASSERT_FALSE(v.as_result()->is_success);

    const auto& err = v.as_result()->owned_inner->as_record();
    ASSERT_EQ(err->type_name, std::string{"ParseError"});
    ASSERT_TRUE(err->find_field("line")->as_integer() >= 1);
    ASSERT_TRUE(err->find_field("column")->as_integer() >= 1);
}

int main() {
    RUN(test_xml_add_child);
    RUN(test_xml_attribute);
    RUN(test_xml_child_count);
    RUN(test_xml_children);
    RUN(test_xml_element);
    RUN(test_xml_has_attribute);
    RUN(test_xml_is_leaf);
    RUN(test_xml_is_valid);
    RUN(test_xml_is_valid_false);
    RUN(test_xml_module);
    RUN(test_xml_parse_serialize);
    RUN(test_xml_serialize_pretty);
    RUN(test_xml_set_attribute);
    RUN(test_xml_set_tag);
    RUN(test_xml_set_text);
    RUN(test_xml_tag);
    RUN(test_xml_text);
    RUN(test_xml_remove_attribute);
    RUN(test_xml_attributes_dict);
    RUN(test_xml_set_cdata_serialize);
    RUN(test_xml_add_comment_serialize);
    RUN(test_xml_to_dictionary);
    RUN(test_xml_from_dictionary);
    RUN(test_xml_children_by_tag);
    RUN(test_xml_has_child);
    RUN(test_xml_find);
    RUN(test_xml_find_all);
    RUN(test_xml_find_by_attribute);
    RUN(test_xml_at);
    RUN(test_xml_text_at);
    RUN(test_xml_find_descendant);
    RUN(test_xml_find_descendant_not_found);
    RUN(test_xml_find_all_descendants);
    RUN(test_xml_get_path);
    RUN(test_xml_get_path_indexed);
    RUN(test_xml_get_path_missing);
    RUN(test_xml_remove_child);
    RUN(test_xml_remove_child_removes_correct);
    RUN(test_xml_remove_child_out_of_bounds);
    RUN(test_xml_replace_child);
    RUN(test_xml_replace_child_out_of_bounds);
    RUN(test_xml_inner_text);
    RUN(test_xml_escape);
    RUN(test_xml_unescape);
    RUN(test_xml_escape_unescape_roundtrip);
    RUN(test_xml_unescape_numeric);
    RUN(test_xml_unescape_malformed);
    RUN(test_xml_serialize_compact);
    RUN(test_xml_entity_decode);
    RUN(test_xml_serialize_escapes_text);
    RUN(test_xml_cdata_roundtrip);
    RUN(test_xml_comment_roundtrip);
    RUN(test_xml_self_closing);
    RUN(test_xml_declaration_skipped);
    RUN(test_xml_attribute_entity_roundtrip);
    RUN(test_xml_attribute_not_found);
    RUN(test_xml_text_no_content);
    RUN(test_xml_find_not_found);
    RUN(test_xml_find_by_attribute_not_found);
    RUN(test_xml_at_missing_path);
    RUN(test_xml_text_at_missing_path);
    RUN(test_xml_deserialize_unclosed);
    RUN(test_xml_deserialize_empty);
    RUN(test_xml_deserialize_unterminated_attributes);
    RUN(test_xml_deserialize_mismatched_tag);
    RUN(test_xml_deserialize_trailing_content);
    RUN(test_xml_deserialize_doctype_rejected);
    RUN(test_xml_is_valid_doctype);
    RUN(test_xml_deserialize_too_deep);
    RUN(test_xml_wrong_type_throws);
    RUN(test_xml_deserialize_file_missing);
    RUN(test_xml_deserialize_file_rejects_oversized_file);
    RUN(test_xml_invalid_names_rejected);
    RUN(test_xml_valid_punctuation_names_round_trip);
    RUN(test_xml_to_node_element);
    RUN(test_xml_to_node_child_variants);
    RUN(test_xml_to_node_cdata);
    RUN(test_xml_to_node_empty_element);
    RUN(test_xml_deserialize_detailed_success);
    RUN(test_xml_deserialize_detailed_failure_message);
    RUN(test_xml_deserialize_detailed_failure_location);
    return SUMMARY();
}

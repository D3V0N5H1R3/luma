// Standard library tests: Queue.

#include "stdlib_test_helpers.hpp"

static void test_queue_concat() {
    const auto v = eval("Queue.to_array(Queue.concat("
                        "Queue.from_array([1, 2]),"
                        "Queue.from_array([3, 4])))");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 4U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[3].as_integer(), 4);
}

static void test_queue_enqueue_dequeue() {
    const auto result =
        eval("Queue.enqueue(Queue.new(), 42) |> Queue.dequeue() |> Result.unwrap()");

    ASSERT_TRUE(result.is_tuple());

    auto& elems = result.as_tuple()->elements;

    ASSERT_EQ(elems[0].as_integer(), 42);
}

static void test_queue_from_array() {
    const auto result = eval("Queue.from_array([1, 2, 3]) |> Queue.length()");

    ASSERT_EQ(result.as_integer(), 3);
}

static void test_queue_is_empty() {
    const auto result = eval(R"(
        Queue.is_empty(Queue.new())
    )");

    ASSERT_TRUE(result.is_truthy());
}

static void test_queue_module() {
    const auto env = luma::test::make_std_env();

    ASSERT_TRUE(env->has("Queue.new"));
    ASSERT_TRUE(env->has("Queue.from_array"));
    ASSERT_TRUE(env->has("Queue.enqueue"));
    ASSERT_TRUE(env->has("Queue.dequeue"));
    ASSERT_TRUE(env->has("Queue.peek"));
    ASSERT_TRUE(env->has("Queue.length"));
    ASSERT_TRUE(env->has("Queue.is_empty"));
    ASSERT_TRUE(env->has("Queue.to_array"));
    ASSERT_TRUE(env->has("Queue.map"));
    ASSERT_TRUE(env->has("Queue.filter"));
    ASSERT_TRUE(env->has("Queue.reduce"));
    ASSERT_TRUE(env->has("Queue.each"));
    ASSERT_TRUE(env->has("Queue.partition"));
    ASSERT_TRUE(env->has("Queue.concat"));
}

static void test_queue_new_length_zero() {
    ASSERT_EQ(eval("Queue.new() |> Queue.length()").as_integer(), 0);
}

static void test_queue_enqueue_fifo_order() {
    // Enqueue appends to the back; to_array preserves FIFO insertion order.
    const auto v = eval("Queue.new()"
                        " |> Queue.enqueue(1)"
                        " |> Queue.enqueue(2)"
                        " |> Queue.enqueue(3)"
                        " |> Queue.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
    ASSERT_EQ((*v.as_array()->elements)[1].as_integer(), 2);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 3);
}

static void test_queue_dequeue_remainder() {
    // Dequeue returns (front, remaining); the remainder keeps FIFO order.
    const auto v = eval("Queue.from_array([1, 2, 3])"
                        " |> Queue.dequeue()"
                        " |> Result.unwrap()");

    ASSERT_TRUE(v.is_tuple());

    const auto& elems = v.as_tuple()->elements;

    ASSERT_EQ(elems[0].as_integer(), 1);

    const auto& rest = elems[1].as_queue()->elements;

    ASSERT_EQ(rest.size(), 2U);
    ASSERT_EQ(rest[0].as_integer(), 2);
    ASSERT_EQ(rest[1].as_integer(), 3);
}

static void test_queue_enqueue_is_immutable() {
    // Enqueue is persistent: the source queue is left unchanged.
    const auto v = eval(R"(
        queue<integer> original = Queue.from_array([1, 2])
        queue<integer> extended = Queue.enqueue(original, 3)
        Queue.length(original) + 100 * Queue.length(extended)
    )");

    ASSERT_EQ(v.as_integer(), 302);
}

static void test_queue_peek_does_not_remove() {
    // Peek inspects the front without consuming it.
    const auto v = eval(R"(
        queue<integer> q = Queue.from_array([10, 20])
        integer front = Queue.peek(q) |> Result.unwrap()
        front + 100 * Queue.length(q)
    )");

    ASSERT_EQ(v.as_integer(), 210);
}

static void test_queue_map() {
    const auto v = eval("Queue.from_array([1, 2, 3])"
                        " |> Queue.map((integer x) -> x * 10)"
                        " |> Result.unwrap()"
                        " |> Queue.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 10);
    ASSERT_EQ((*v.as_array()->elements)[2].as_integer(), 30);
}

static void test_queue_filter() {
    const auto v = eval("Queue.from_array([1, 2, 3, 4])"
                        " |> Queue.filter((integer x) -> x % 2 == 0)"
                        " |> Result.unwrap()"
                        " |> Queue.length()");

    ASSERT_EQ(v.as_integer(), 2);
}

static void test_queue_reduce() {
    const auto v = eval("Queue.from_array([1, 2, 3, 4])"
                        " |> Queue.reduce(0, (integer acc, integer x) -> acc + x)"
                        " |> Result.unwrap()");

    ASSERT_EQ(v.as_integer(), 10);
}

static void test_queue_each() {
    const auto v = eval("Queue.from_array([1, 2, 3]) |> Queue.each((integer x) -> x)");

    ASSERT_RESULT_SUCCESS(v);
}

static void test_queue_to_array() {
    const auto v = eval("Queue.from_array([1, 2, 3]) |> Queue.to_array()");

    ASSERT_TRUE(v.is_array());
    ASSERT_EQ(v.as_array()->elements->size(), 3U);
    ASSERT_EQ((*v.as_array()->elements)[0].as_integer(), 1);
}

static void test_queue_partition() {
    const auto v = eval("Queue.partition("
                        "Queue.from_array([1, 2, 3, 4, 5]),"
                        "(integer x) -> x % 2 == 0)");

    ASSERT_RESULT_SUCCESS(v);

    const auto& tup = v.as_result()->owned_inner->as_tuple()->elements;

    ASSERT_EQ(tup.size(), 2U);
    ASSERT_EQ(tup[0].as_queue()->elements.size(), 2U);
    ASSERT_EQ(tup[1].as_queue()->elements.size(), 3U);
}

static void test_queue_peek() {
    const auto result = eval("Queue.from_array([10, 20]) |> Queue.peek() |> Result.unwrap()");

    ASSERT_EQ(result.as_integer(), 10);
}

// ─── Negative: empty-container failures ───────────────────────────

static void test_queue_dequeue_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("Queue.new() |> Queue.dequeue()"));
}

static void test_queue_peek_empty_fails() {
    ASSERT_RESULT_FAILURE(eval("Queue.new() |> Queue.peek()"));
}

// ─── Negative: wrong-argument-type errors ─────────────────────────

static void test_queue_length_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Queue.length(42)"), "queue");
}

static void test_queue_enqueue_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Queue.enqueue(42, 1)"), "queue");
}

static void test_queue_dequeue_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval("Queue.dequeue(42)"), "queue");
}

static void test_queue_peek_wrong_type_throws() {
    ASSERT_THROWS_WITH_MESSAGE(eval(R"(Queue.peek("x"))"), "queue");
}

static void test_queue_from_array_wrong_type_throws() {
    ASSERT_THROWS(eval("Queue.from_array(42)"));
}

static void test_queue_concat_wrong_type_throws() {
    ASSERT_THROWS(eval("Queue.concat(Queue.new(), 42)"));
}

int main() {
    RUN(test_queue_concat);
    RUN(test_queue_enqueue_dequeue);
    RUN(test_queue_from_array);
    RUN(test_queue_is_empty);
    RUN(test_queue_module);
    RUN(test_queue_new_length_zero);
    RUN(test_queue_enqueue_fifo_order);
    RUN(test_queue_dequeue_remainder);
    RUN(test_queue_enqueue_is_immutable);
    RUN(test_queue_peek_does_not_remove);
    RUN(test_queue_map);
    RUN(test_queue_filter);
    RUN(test_queue_reduce);
    RUN(test_queue_each);
    RUN(test_queue_to_array);
    RUN(test_queue_partition);
    RUN(test_queue_peek);
    RUN(test_queue_dequeue_empty_fails);
    RUN(test_queue_peek_empty_fails);
    RUN(test_queue_length_wrong_type_throws);
    RUN(test_queue_enqueue_wrong_type_throws);
    RUN(test_queue_dequeue_wrong_type_throws);
    RUN(test_queue_peek_wrong_type_throws);
    RUN(test_queue_from_array_wrong_type_throws);
    RUN(test_queue_concat_wrong_type_throws);
    return SUMMARY();
}

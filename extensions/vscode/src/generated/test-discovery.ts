// AUTO-GENERATED from extensions/shared/test-discovery-pattern.json
// Do not edit manually. Run: python generate-test-discovery.py --vscode

/** Matches an @test-annotated function declaration, also when indented inside a namespace. Capture group 1 is the function name. Allows preceding annotations and an optional generic/qualified return type. */
export function testFunctionPattern(): RegExp {
    return /^[ \t]*(?:@\w+\s+)*@test\s*\n?\s*function\s+(?:<[^>]*>\s+)?[\w<>,\s[\]().|]+?\s+(\w+)\s*\(/gm;
}

/** Matches an @test annotation line, also when indented or followed by a comment (the file contains tests). */
export function testAnnotationPattern(): RegExp {
    return /^[ \t]*@test\s*(?:#.*)?$/m;
}

/** Matches an @main annotation line, also when indented or followed by a comment (the file is runnable). */
export function mainAnnotationPattern(): RegExp {
    return /^[ \t]*@main\s*(?:#.*)?$/m;
}

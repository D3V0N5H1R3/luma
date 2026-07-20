// AUTO-GENERATED from extensions/shared/test-discovery-pattern.json
// Do not edit manually. Run: python generate-test-discovery.py --vscode

/** Matches an @test-annotated function declaration. Capture group 1 is the function name. Allows preceding annotations and an optional generic/qualified return type. */
export function testFunctionPattern(): RegExp {
    return /^(?:@\w+\s+)*@test\s*\n?\s*function\s+(?:<[^>]*>\s+)?[\w<>,\s[\]().|]+?\s+(\w+)\s*\(/gm;
}

/** Matches a standalone @test annotation line (the file contains tests). */
export function testAnnotationPattern(): RegExp {
    return /^@test\s*$/m;
}

/** Matches a standalone @main annotation line (the file is runnable). */
export function mainAnnotationPattern(): RegExp {
    return /^@main\s*$/m;
}

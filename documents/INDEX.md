# Document Index

Section-level table of contents for the reference documents in this directory.
Use this to locate specific content without reading entire files — most documents
are 50–100 KB and reading them in full consumes significant context budget.

## How to use

1. Scan this index for the topic you need.
2. Note the file and line range.
3. Read only that section (e.g. `view_range: [46, 180]`).

---

## CLAUDE.md

| Section | Lines |
| ------- | ----- |
| Entire document | 1–13 |

## Luma_Coding_Guidelines.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 9–39 |
| 1 — Design Philosophy | 40–51 |
| 2 — Naming Conventions | 52–91 |
| 3 — Formatting and Whitespace | 92–140 |
| 4 — Comments | 141–184 |
| 5 — Variables and Mutability | 185–237 |
| 6 — Type Annotations | 238–315 |
| 7 — Functions | 316–369 |
| 8 — Control Flow | 370–433 |
| 9 — Error Handling | 434–593 |
| 10 — Match Expressions | 594–685 |
| 11 — Collections | 686–784 |
| 12 — Strings | 785–837 |
| 13 — Pipe Operator | 838–903 |
| 14 — Lambdas and Higher-Order Functions | 904–946 |
| 15 — Records | 947–1018 |
| 16 — Choice Types | 1019–1099 |
| 17 — Choice Types — Advanced | 1100–1116 |
| 18 — Interfaces | 1117–1143 |
| 19 — Namespaces | 1144–1275 |
| 20 — Concurrency | 1276–1346 |
| 21 — Testing | 1347–1446 |
| 22 — File Organisation | 1447–1532 |
| 23 — Anti-Patterns | 1533–1677 |
| 24 — Security and Resource Limits | 1678–1704 |
| See Also | 1705–1713 |

## Luma_Concurrent_Debugging_Guide.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 9–22 |
| 1 — Concurrency Model Overview | 23–88 |
| 2 — How DAP Presents Concurrent Tasks | 89–110 |
| 3 — Breakpoints in Concurrent Code | 111–149 |
| 4 — Inspecting Channel State | 150–174 |
| 5 — Debugging Deadlocks and Races | 175–218 |
| 6 — Common Pitfalls | 219–260 |
| 7 — Step-by-Step Example | 261–327 |
| See Also | 328–334 |

## Luma_Debugger.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–36 |
| 1 — Overview | 37–46 |
| 2 — Goals | 47–60 |
| 3 — Non-Goals | 61–69 |
| 4 — Architecture | 70–112 |
| 5 — Supported DAP Requests | 113–172 |
| 6 — Breakpoints | 173–203 |
| 7 — Stepping | 204–218 |
| 8 — Variable Inspection | 219–234 |
| 9 — Concurrency Support | 235–249 |
| 10 — Exception Handling | 250–266 |
| 11 — Platform Support | 267–276 |
| 12 — Usage | 277–304 |
| 13 — Editor Integration | 305–349 |
| 14 — File Layout | 350–428 |
| 15 — Module Responsibilities | 429–768 |
| 16 — Data Flow | 769–800 |
| 17 — VM Instrumentation | 801–859 |
| 18 — Local Variable Names | 860–876 |
| 19 — Output Capture | 877–884 |
| 20 — Error Handling | 885–897 |
| 21 — CMake Integration | 898–919 |
| 22 — Testing Strategy | 920–965 |
| 23 — Future Extensions | 966–975 |
| See Also | 976–981 |

## Luma_Error_Handling.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 9–24 |
| 1 — Error Categories | 25–39 |
| 2 — Domain Failures — `result<T>` | 40–134 |
| 3 — Absent Values — `optional<T>` | 135–190 |
| 4 — Programmer Errors — Runtime Errors | 191–223 |
| 5 — Try / Catch / Finally | 224–271 |
| 6 — Standard Library Conventions | 272–320 |
| 7 — Third-Party Library Conventions | 321–371 |
| 8 — Anti-Patterns | 372–468 |
| 9 — Interpreter Implementation Policy | 469–527 |
| See Also | 528–538 |

## Luma_Initial_Concept.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 5–19 |
| 1 — Objective | 20–31 |
| 2 — Details | 32–140 |
| 3 — Next | 141–148 |
| See Also | 149–153 |

## Luma_Installation_Guide.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–23 |
| 1 — Overview | 24–37 |
| 2 — Download | 38–55 |
| 3 — Verify Checksums | 56–84 |
| 4 — Windows | 85–121 |
| 5 — macOS | 122–170 |
| 6 — Linux | 171–219 |
| 7 — Editor Setup — Visual Studio Code | 220–275 |
| 8 — Editor Setup — Zed | 276–286 |
| 9 — Verify the Installation | 287–329 |
| 10 — Building from Source | 330–343 |
| 11 — Troubleshooting | 344–363 |
| See Also | 364–374 |

## Luma_Language_Server.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–36 |
| 1 — Overview | 37–44 |
| 2 — Goals | 45–54 |
| 3 — Non-Goals | 55–62 |
| 4 — Architecture | 63–86 |
| 5 — Supported LSP Methods | 87–139 |
| 6 — Diagnostics | 140–160 |
| 7 — Hover | 161–172 |
| 8 — Completion | 173–184 |
| 9 — Document Management | 185–192 |
| 10 — Standard Library Support | 193–203 |
| 11 — Platform Support | 204–211 |
| 12 — Usage | 212–267 |
| 13 — Editor Integration | 268–279 |
| 14 — File Layout | 280–438 |
| 15 — Module Responsibilities | 439–860 |
| 16 — Data Flow | 861–985 |
| 17 — JSON-RPC Dispatch | 986–1028 |
| 18 — Capability Negotiation | 1029–1079 |
| 19 — Stdlib Signature Access | 1080–1092 |
| 20 — Build Integration | 1093–1130 |
| 21 — Error Handling | 1131–1141 |
| 22 — Logging | 1142–1154 |
| 23 — Platform-Specific Handling | 1155–1169 |
| See Also | 1170–1175 |

## Luma_Manual_Tests.md

| Section | Lines |
| ------- | ----- |
| Prerequisites | 5–156 |
| Part 1 — Interpreter | 158–457 |
| Part 2 — Language Server (VS Code) | 460–544 |
| Part 3 — Debugger (VS Code) | 547–625 |
| Part 4 — VS Code Extension Features | 628–670 |
| Part 5 — Zed Extension | 673–723 |
| Part 6 — Cross-Cutting & Regression Tests | 726–754 |
| Execution Checklist | 757–769 |
| Reporting Issues | 771–779 |

## Luma_Performance_Guide.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 5–19 |
| 1 — Immutability and Deep Copies | 20–31 |
| 2 — Collection Performance Comparison | 32–50 |
| 3 — String Building | 51–70 |
| 4 — Recursion Limits | 71–74 |
| 5 — Loop Iteration Limits | 75–78 |
| 6 — Resource Limits | 79–93 |
| 7 — Interpreter Optimisations | 94–104 |
| See Also | 105–111 |

## Luma_REPL_Guide.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–23 |
| 1 — Starting the REPL | 24–54 |
| 2 — Evaluating Expressions | 55–78 |
| 3 — Variables and Functions | 79–119 |
| 4 — Multi-Line Input | 120–138 |
| 5 — Pipe Operator | 139–156 |
| 6 — Using Standard Library Modules | 157–186 |
| 7 — REPL Commands | 187–228 |
| 8 — Line Editing and Tab Completion | 229–272 |
| 9 — Error Handling | 273–315 |
| 10 — Tips and Tricks | 316–328 |
| See Also | 329–335 |

## Luma_Software_Architecture.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–32 |
| 1 — Introduction | 33–55 |
| 2 — Design Goals and Constraints | 56–78 |
| 3 — High-Level Architecture | 79–114 |
| 4 — Module Decomposition | 115–858 |
| 5 — Data Structures | 859–1024 |
| 6 — Processing Pipeline | 1025–1135 |
| 7 — Bytecode Compiler and Virtual Machine Internals | 1136–1774 |
| 8 — Type System Design | 1775–1820 |
| 9 — Memory Management Strategy | 1821–1852 |
| 10 — Error Handling Strategy | 1853–1896 |
| 11 — Standard Library Architecture | 1897–2015 |
| 12 — Concurrency Architecture | 2016–2115 |
| 13 — REPL Architecture | 2116–2143 |
| 14 — Testing Architecture | 2144–2171 |
| 15 — File Inclusion and Source Management | 2172–2202 |
| 16 — Project File Structure | 2203–2276 |
| 17 — Cross-Platform Considerations | 2277–2306 |
| 18 — Debugger Architecture | 2307–2329 |
| 19 — Design Decisions and Rationale | 2330–2421 |
| See Also | 2422–2431 |

## Luma_Standard_Library_Reference.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 9–54 |
| 1 — Core Built-Ins | 55–67 |
| 2 — Array | 68–138 |
| 3 — Bits | 139–167 |
| 4 — Calculus | 168–197 |
| 5 — Channel | 198–237 |
| 6 — Compression | 238–286 |
| 7 — Console | 287–308 |
| 8 — Converter | 309–333 |
| 9 — Csv | 334–376 |
| 10 — DateTime | 377–519 |
| 11 — Decimal | 520–608 |
| 12 — Dictionary | 609–651 |
| 13 — Encoder | 652–682 |
| 14 — FileSystem | 683–770 |
| 15 — Hash | 771–812 |
| 16 — Http | 813–920 |
| 17 — Json | 921–1017 |
| 18 — KeyValueStore | 1018–1045 |
| 19 — LinearAlgebra | 1046–1104 |
| 20 — Log | 1105–1130 |
| 21 — Math | 1131–1266 |
| 22 — Optional | 1267–1323 |
| 23 — Order | 1324–1387 |
| 24 — Process | 1388–1486 |
| 25 — Queue | 1487–1513 |
| 26 — Random | 1514–1569 |
| 27 — Reference | 1570–1626 |
| 28 — RegularExpression | 1627–1729 |
| 29 — Resource | 1730–1782 |
| 30 — Result | 1783–1815 |
| 31 — Set | 1816–1850 |
| 32 — Socket | 1851–1918 |
| 33 — Stack | 1919–1946 |
| 34 — Statistics | 1947–1962 |
| 35 — String | 1963–2068 |
| 36 — Task | 2069–2138 |
| 37 — Terminal | 2139–2355 |
| 38 — Xml | 2356–2434 |
| See Also | 2435–2440 |

## Luma_Syntax_Highlighting.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–38 |
| 1 — Token Design | 39–307 |
| 2 — Visual Studio Code Extension | 308–522 |
| 3 — Zed Extension | 523–719 |
| See Also | 720–723 |

## Luma_Tutorial.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–38 |
| 1 — What Programming Is, and What Luma Is | 39–65 |
| 2 — Installing and Running Luma | 66–101 |
| 3 — Your First Program | 102–144 |
| 4 — Printing and Comments | 145–175 |
| 5 — Values and Types | 176–236 |
| 6 — Variables and Mutability | 237–287 |
| 7 — Arithmetic and Operators | 288–354 |
| 8 — Working with Text | 355–436 |
| 9 — Making Decisions | 437–490 |
| 10 — Repeating Work | 491–595 |
| 11 — Arrays | 596–682 |
| 12 — Dictionaries | 683–747 |
| 13 — Functions | 748–840 |
| 14 — Records | 841–921 |
| 15 — Tuples | 922–963 |
| 16 — Choice Types | 964–1012 |
| 17 — Pattern Matching | 1013–1080 |
| 18 — Handling Absence and Failure | 1081–1153 |
| 19 — Lambdas, Pipes, and Higher-Order Functions | 1154–1246 |
| 20 — A Tour of the Standard Library | 1247–1334 |
| 21 — Reading Input from the User | 1335–1383 |
| 22 — Testing Your Code | 1384–1435 |
| 23 — Organising Code with Namespaces | 1436–1555 |
| 24 — Splitting a Program Across Files | 1556–1639 |
| 25 — Project — A Number-Guessing Game | 1640–1723 |
| 26 — Where to Go Next | 1724–1742 |
| 27 — Glossary | 1743–1785 |
| See Also | 1786–1795 |

## Luma_User_Manual.md

| Section | Lines |
| ------- | ----- |
| Table of Contents | 7–45 |
| 1 — Getting Started | 46–148 |
| 2 — Types | 149–247 |
| 3 — Variables and Mutability | 248–370 |
| 4 — Operators | 371–583 |
| 5 — Control Flow | 584–777 |
| 6 — Functions | 778–888 |
| 7 — Lambdas | 889–949 |
| 8 — Records | 950–1112 |
| 9 — Arrays | 1113–1211 |
| 10 — Dictionaries | 1212–1275 |
| 11 — Tuples | 1276–1347 |
| 12 — Choice Types — Unit Variants | 1348–1401 |
| 13 — Choice Types (ADTs) | 1402–1516 |
| 14 — Result and Optional | 1517–1752 |
| 15 — Match | 1753–2004 |
| 16 — String Interpolation and Multi-Line Strings | 2005–2063 |
| 17 — Pipe Operator | 2064–2112 |
| 18 — Named Arguments | 2113–2139 |
| 19 — Type Aliases | 2140–2165 |
| 20 — Interfaces | 2166–2245 |
| 21 — Generics, `downcast`, and `is` | 2246–2479 |
| 22 — Namespaces and `use` | 2480–2680 |
| 23 — Ownership (`unique` and `borrow`) | 2681–2716 |
| 24 — Testing with `@test` | 2717–2769 |
| 25 — Including Files | 2770–2825 |
| 26 — Standard Library Reference | 2826–2833 |
| 27 — Linter and `--strict` Mode | 2834–2889 |
| 28 — Reserved Keywords | 2890–2916 |
| 29 — Error Reference | 2917–3238 |
| 30 — Complete Programs | 3239–3448 |
| 31 — Debugging | 3449–3473 |
| 32 — Formal Grammar (EBNF) | 3474–3828 |
| See Also | 3829–3842 |

# Document Index

Section-level table of contents for the reference documents in this directory. Use this to locate specific content without reading entire files — the larger documents run to hundreds of kilobytes, and reading them in full consumes significant context budget.

## How to Use

1. Scan this index for the topic you need.
2. Note the file and line range.
3. Read only that section (e.g. `view_range: [46, 180]`).

> **Note:** Line ranges drift whenever a document is edited. After changing any document listed here, update its ranges in this index.

---

## Luma_Coding_Guidelines.md

| Section                                 | Lines     |
| --------------------------------------- | --------- |
| Table of Contents                       | 9–36      |
| 1 — Design Philosophy                   | 40–48     |
| 2 — Naming Conventions                  | 52–88     |
| 3 — Formatting and Whitespace           | 92–137    |
| 4 — Comments                            | 141–181   |
| 5 — Variables and Mutability            | 185–234   |
| 6 — Type Annotations                    | 238–312   |
| 7 — Functions                           | 316–366   |
| 8 — Control Flow                        | 370–430   |
| 9 — Error Handling                      | 434–590   |
| 10 — Match Expressions                  | 594–682   |
| 11 — Collections                        | 686–781   |
| 12 — Strings                            | 785–834   |
| 13 — Pipe Operator                      | 838–900   |
| 14 — Lambdas and Higher-Order Functions | 904–943   |
| 15 — Records                            | 947–1015  |
| 16 — Choice Types                       | 1019–1096 |
| 17 — Choice Types — Advanced            | 1100–1113 |
| 18 — Interfaces                         | 1117–1140 |
| 19 — Namespaces                         | 1144–1272 |
| 20 — Concurrency                        | 1276–1343 |
| 21 — Testing                            | 1347–1443 |
| 22 — File Organisation                  | 1447–1529 |
| 23 — Anti-Patterns                      | 1533–1674 |
| 24 — Security and Resource Limits       | 1678–1701 |
| See Also                                | 1705–1713 |

## Luma_Concurrent_Debugging_Guide.md

| Section                               | Lines   |
| ------------------------------------- | ------- |
| Table of Contents                     | 9–19    |
| 1 — Concurrency Model Overview        | 23–85   |
| 2 — How DAP Presents Concurrent Tasks | 89–107  |
| 3 — Breakpoints in Concurrent Code    | 111–146 |
| 4 — Inspecting Channel State          | 150–171 |
| 5 — Debugging Deadlocks and Races     | 175–215 |
| 6 — Common Pitfalls                   | 219–257 |
| 7 — Step-by-Step Example              | 261–324 |
| See Also                              | 328–334 |

## Luma_Debugger.md

| Section                      | Lines   |
| ---------------------------- | ------- |
| Table of Contents            | 7–33    |
| 1 — Overview                 | 37–43   |
| 2 — Goals                    | 47–57   |
| 3 — Non-Goals                | 61–66   |
| 4 — Architecture             | 70–109  |
| 5 — Supported DAP Requests   | 113–169 |
| 6 — Breakpoints              | 173–200 |
| 7 — Stepping                 | 204–215 |
| 8 — Variable Inspection      | 219–231 |
| 9 — Concurrency Support      | 235–246 |
| 10 — Exception Handling      | 250–263 |
| 11 — Platform Support        | 267–273 |
| 12 — Usage                   | 277–301 |
| 13 — Editor Integration      | 305–346 |
| 14 — File Layout             | 350–425 |
| 15 — Module Responsibilities | 429–765 |
| 16 — Data Flow               | 769–797 |
| 17 — VM Instrumentation      | 801–856 |
| 18 — Local Variable Names    | 860–873 |
| 19 — Output Capture          | 877–881 |
| 20 — Error Handling          | 885–894 |
| 21 — CMake Integration       | 898–916 |
| 22 — Testing Strategy        | 920–962 |
| 23 — Future Extensions       | 966–972 |
| See Also                     | 976–982 |

## Luma_Error_Handling.md

| Section                                | Lines   |
| -------------------------------------- | ------- |
| Table of Contents                      | 9–21    |
| 1 — Error Categories                   | 25–36   |
| 2 — Domain Failures — `result<T>`      | 40–131  |
| 3 — Absent Values — `optional<T>`      | 135–187 |
| 4 — Programmer Errors — Runtime Errors | 191–220 |
| 5 — Try / Catch / Finally              | 224–268 |
| 6 — Standard Library Conventions       | 272–317 |
| 7 — Third-Party Library Conventions    | 321–368 |
| 8 — Anti-Patterns                      | 372–465 |
| 9 — Interpreter Implementation Policy  | 469–524 |
| See Also                               | 528–538 |

## Luma_Initial_Concept.md

| Section           | Lines   |
| ----------------- | ------- |
| Table of Contents | 5–18    |
| 1 — Objective     | 22–30   |
| 2 — Details       | 34–139  |
| 3 — Next          | 143–147 |
| See Also          | 151–155 |

## Luma_Installation_Guide.md

| Section                               | Lines   |
| ------------------------------------- | ------- |
| Table of Contents                     | 7–21    |
| 1 — Overview                          | 25–35   |
| 2 — Download                          | 39–53   |
| 3 — Verify Checksums                  | 57–82   |
| 4 — Windows                           | 86–119  |
| 5 — macOS                             | 123–168 |
| 6 — Linux                             | 172–217 |
| 7 — Editor Setup — Visual Studio Code | 221–273 |
| 8 — Editor Setup — Zed                | 277–284 |
| 9 — Verify the Installation           | 288–327 |
| 10 — Building from Source             | 331–341 |
| 11 — Troubleshooting                  | 345–361 |
| See Also                              | 365–374 |

## Luma_Language_Server.md

| Section                         | Lines     |
| ------------------------------- | --------- |
| Table of Contents               | 7–33      |
| 1 — Overview                    | 37–41     |
| 2 — Goals                       | 45–51     |
| 3 — Non-Goals                   | 55–59     |
| 4 — Architecture                | 63–83     |
| 5 — Supported LSP Methods       | 87–136    |
| 6 — Diagnostics                 | 140–157   |
| 7 — Hover                       | 161–169   |
| 8 — Completion                  | 173–181   |
| 9 — Document Management         | 185–189   |
| 10 — Standard Library Support   | 193–200   |
| 11 — Platform Support           | 204–208   |
| 12 — Usage                      | 212–264   |
| 13 — Editor Integration         | 268–276   |
| 14 — File Layout                | 280–435   |
| 15 — Module Responsibilities    | 439–857   |
| 16 — Data Flow                  | 861–982   |
| 17 — JSON-RPC Dispatch          | 986–1025  |
| 18 — Capability Negotiation     | 1029–1076 |
| 19 — Stdlib Signature Access    | 1080–1089 |
| 20 — Build Integration          | 1093–1127 |
| 21 — Error Handling             | 1131–1138 |
| 22 — Logging                    | 1142–1151 |
| 23 — Platform-Specific Handling | 1155–1166 |
| See Also                        | 1170–1176 |

## Luma_Manual_Tests.md

| Section                              | Lines   |
| ------------------------------------ | ------- |
| Table of Contents                    | 5–18    |
| Prerequisites                        | 22–171  |
| 1 — Interpreter                      | 175–473 |
| 2 — Language Server (VS Code)        | 477–560 |
| 3 — Debugger (VS Code)               | 564–641 |
| 4 — VS Code Extension Features       | 645–686 |
| 5 — Zed Extension                    | 690–739 |
| 6 — Cross-Cutting & Regression Tests | 743–770 |
| 7 — Execution Checklist              | 774–786 |
| 8 — Reporting Issues                 | 788–796 |
| See Also                             | 800–806 |

## Luma_Performance_Guide.md

| Section                               | Lines   |
| ------------------------------------- | ------- |
| Table of Contents                     | 5–16    |
| 1 — Immutability and Deep Copies      | 20–30   |
| 2 — Collection Performance Comparison | 32–49   |
| 3 — String Building                   | 51–69   |
| 4 — Recursion Limits                  | 71–73   |
| 5 — Loop Iteration Limits             | 75–77   |
| 6 — Resource Limits                   | 79–92   |
| 7 — Interpreter Optimisations         | 94–102  |
| See Also                              | 106–111 |

## Luma_REPL_Guide.md

| Section                             | Lines   |
| ----------------------------------- | ------- |
| Table of Contents                   | 7–20    |
| 1 — Starting the REPL               | 24–51   |
| 2 — Evaluating Expressions          | 55–75   |
| 3 — Variables and Functions         | 79–116  |
| 4 — Multi-Line Input                | 120–135 |
| 5 — Pipe Operator                   | 139–153 |
| 6 — Using Standard Library Modules  | 157–183 |
| 7 — REPL Commands                   | 187–225 |
| 8 — Line Editing and Tab Completion | 229–269 |
| 9 — Error Handling                  | 273–312 |
| 10 — Tips and Tricks                | 316–325 |
| See Also                            | 329–335 |

## Luma_Sandbox.md

| Section                       | Lines   |
| ----------------------------- | ------- |
| Table of Contents             | 7–19    |
| 1 — Overview                  | 23–27   |
| 2 — Threat Model              | 31–53   |
| 3 — Capability Model          | 57–73   |
| 4 — Guarantees                | 77–87   |
| 5 — Non-Goals and Limitations | 91–100  |
| 6 — Enforcement               | 104–112 |
| 7 — Validation                | 116–122 |
| 8 — Adding a Module Safely    | 126–133 |
| 9 — Recommended Deployment    | 137–143 |
| See Also                      | 147–153 |

## Luma_Software_Architecture.md

| Section                                             | Lines     |
| --------------------------------------------------- | --------- |
| Table of Contents                                   | 7–29      |
| 1 — Introduction                                    | 33–52     |
| 2 — Design Goals and Constraints                    | 56–75     |
| 3 — High-Level Architecture                         | 79–111    |
| 4 — Module Decomposition                            | 115–843   |
| 5 — Data Structures                                 | 847–1009  |
| 6 — Processing Pipeline                             | 1013–1112 |
| 7 — Bytecode Compiler and Virtual Machine Internals | 1116–1751 |
| 8 — Type System Design                              | 1755–1797 |
| 9 — Memory Management Strategy                      | 1801–1829 |
| 10 — Error Handling Strategy                        | 1833–1873 |
| 11 — Standard Library Architecture                  | 1877–1990 |
| 12 — Concurrency Architecture                       | 1994–2090 |
| 13 — REPL Architecture                              | 2094–2118 |
| 14 — Testing Architecture                           | 2122–2146 |
| 15 — File Inclusion and Source Management           | 2150–2177 |
| 16 — Project File Structure                         | 2181–2251 |
| 17 — Cross-Platform Considerations                  | 2255–2281 |
| 18 — Debugger Architecture                          | 2285–2304 |
| 19 — Design Decisions and Rationale                 | 2308–2400 |
| See Also                                            | 2404–2413 |

## Luma_Standard_Library_Reference.md

| Section                | Lines     |
| ---------------------- | --------- |
| Table of Contents      | 9–50      |
| 1 — Core Built-Ins     | 54–65     |
| 2 — Array              | 67–136    |
| 3 — Bits               | 138–160   |
| 4 — Calculus           | 164–192   |
| 5 — Channel            | 194–232   |
| 6 — Compression        | 234–281   |
| 7 — Console            | 283–303   |
| 8 — Converter          | 305–328   |
| 9 — Csv                | 330–362   |
| 10 — DateTime          | 364–505   |
| 11 — Decimal           | 507–594   |
| 12 — Dictionary        | 596–637   |
| 13 — Encoder           | 639–668   |
| 14 — FileSystem        | 670–756   |
| 15 — Hash              | 758–798   |
| 16 — Http              | 800–905   |
| 17 — Json              | 907–1002  |
| 18 — KeyValueStore     | 1004–1054 |
| 19 — LinearAlgebra     | 1056–1113 |
| 20 — Log               | 1115–1139 |
| 21 — Math              | 1141–1274 |
| 22 — Optional          | 1276–1331 |
| 23 — Order             | 1333–1384 |
| 24 — Process           | 1386–1483 |
| 25 — Queue             | 1485–1510 |
| 26 — Random            | 1512–1566 |
| 27 — Reference         | 1568–1629 |
| 28 — RegularExpression | 1631–1732 |
| 29 — Resource          | 1734–1785 |
| 30 — Result            | 1787–1818 |
| 31 — Set               | 1820–1853 |
| 32 — Socket            | 1855–1919 |
| 33 — Stack             | 1923–1949 |
| 34 — Statistics        | 1951–1961 |
| 35 — String            | 1963–2067 |
| 36 — Task              | 2069–2137 |
| 37 — Terminal          | 2139–2354 |
| 38 — Xml               | 2356–2431 |
| See Also               | 2435–2442 |

## Luma_Syntax_Highlighting.md

| Section                          | Lines   |
| -------------------------------- | ------- |
| Table of Contents                | 7–35    |
| 1 — Token Design                 | 39–305  |
| 2 — Visual Studio Code Extension | 309–505 |
| 3 — Zed Extension                | 509–703 |
| See Also                         | 707–711 |

## Luma_Tutorial.md

| Section                                         | Lines     |
| ----------------------------------------------- | --------- |
| Table of Contents                               | 7–37      |
| 1 — What Programming Is, and What Luma Is       | 41–64     |
| 2 — Installing and Running Luma                 | 68–100    |
| 3 — Your First Program                          | 104–143   |
| 4 — Printing and Comments                       | 147–174   |
| 5 — Values and Types                            | 178–235   |
| 6 — Variables and Mutability                    | 239–286   |
| 7 — Arithmetic and Operators                    | 290–353   |
| 8 — Working with Text                           | 357–437   |
| 9 — Making Decisions                            | 439–489   |
| 10 — Repeating Work                             | 493–594   |
| 11 — Arrays                                     | 598–681   |
| 12 — Dictionaries                               | 685–746   |
| 13 — Functions                                  | 750–841   |
| 14 — Records                                    | 843–920   |
| 15 — Tuples                                     | 924–962   |
| 16 — Choice Types                               | 966–1011  |
| 17 — Pattern Matching                           | 1015–1079 |
| 18 — Handling Absence and Failure               | 1083–1152 |
| 19 — Lambdas, Pipes, and Higher-Order Functions | 1156–1247 |
| 20 — A Tour of the Standard Library             | 1249–1332 |
| 21 — Reading Input from the User                | 1336–1381 |
| 22 — Testing Your Code                          | 1385–1433 |
| 23 — Organising Code with Namespaces            | 1437–1553 |
| 24 — Splitting a Program Across Files           | 1557–1637 |
| 25 — Project — A Number-Guessing Game           | 1641–1721 |
| 26 — Where to Go Next                           | 1725–1739 |
| 27 — Glossary                                   | 1743–1782 |
| See Also                                        | 1786–1794 |

## Luma_User_Manual.md

| Section                                          | Lines     |
| ------------------------------------------------ | --------- |
| Table of Contents                                | 7–42      |
| 1 — Getting Started                              | 46–144    |
| 2 — Types                                        | 148–242   |
| 3 — Variables and Mutability                     | 246–359   |
| 4 — Operators                                    | 363–565   |
| 5 — Control Flow                                 | 569–759   |
| 6 — Functions                                    | 763–870   |
| 7 — Lambdas                                      | 874–941   |
| 8 — Records                                      | 945–1099  |
| 9 — Arrays                                       | 1103–1198 |
| 10 — Dictionaries                                | 1202–1262 |
| 11 — Tuples                                      | 1266–1334 |
| 12 — Choice Types — Unit Variants                | 1338–1388 |
| 13 — Choice Types (ADTs)                         | 1392–1503 |
| 14 — Result and Optional                         | 1507–1741 |
| 15 — Match                                       | 1745–1993 |
| 16 — String Interpolation and Multi-Line Strings | 1997–2052 |
| 17 — Pipe Operator                               | 2056–2101 |
| 18 — Named Arguments                             | 2105–2128 |
| 19 — Type Aliases                                | 2132–2154 |
| 20 — Interfaces                                  | 2158–2234 |
| 21 — Generics, `downcast`, and `is`              | 2238–2468 |
| 22 — Namespaces and `use`                        | 2472–2667 |
| 23 — Ownership (`unique` and `borrow`)           | 2671–2703 |
| 24 — Testing with `@test`                        | 2707–2756 |
| 25 — Including Files                             | 2760–2811 |
| 26 — Standard Library Reference                  | 2815–2819 |
| 27 — Linter and `--strict` Mode                  | 2823–2875 |
| 28 — Reserved Keywords                           | 2879–2898 |
| 29 — Error Reference                             | 2902–3220 |
| 30 — Complete Programs                           | 3224–3430 |
| 31 — Debugging                                   | 3434–3455 |
| 32 — Formal Grammar (EBNF)                       | 3459–3810 |
| See Also                                         | 3814–3826 |

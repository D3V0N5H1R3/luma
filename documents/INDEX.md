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
| 6 — Type Annotations                    | 238–313   |
| 7 — Functions                           | 317–367   |
| 8 — Control Flow                        | 371–431   |
| 9 — Error Handling                      | 435–594   |
| 10 — Match Expressions                  | 598–686   |
| 11 — Collections                        | 690–785   |
| 12 — Strings                            | 789–838   |
| 13 — Pipe Operator                      | 842–904   |
| 14 — Lambdas and Higher-Order Functions | 908–947   |
| 15 — Records                            | 951–1019  |
| 16 — Choice Types                       | 1023–1100 |
| 17 — Choice Types — Advanced            | 1104–1117 |
| 18 — Interfaces                         | 1121–1144 |
| 19 — Namespaces                         | 1148–1276 |
| 20 — Concurrency                        | 1280–1349 |
| 21 — Testing                            | 1353–1453 |
| 22 — File Organisation                  | 1457–1542 |
| 23 — Anti-Patterns                      | 1546–1687 |
| 24 — Security and Resource Limits       | 1691–1714 |
| See Also                                | 1718–1726 |

## Luma_Concurrent_Debugging_Guide.md

| Section                               | Lines   |
| ------------------------------------- | ------- |
| Table of Contents                     | 9–19    |
| 1 — Concurrency Model Overview        | 23–90   |
| 2 — How DAP Presents Concurrent Tasks | 94–112  |
| 3 — Breakpoints in Concurrent Code    | 116–151 |
| 4 — Inspecting Channel State          | 155–176 |
| 5 — Debugging Deadlocks and Races     | 180–220 |
| 6 — Common Pitfalls                   | 224–264 |
| 7 — Step-by-Step Example              | 268–332 |
| See Also                              | 336–342 |

## Luma_Debugger.md

| Section                      | Lines   |
| ---------------------------- | ------- |
| Table of Contents            | 7–33    |
| 1 — Overview                 | 37–43   |
| 2 — Goals                    | 47–57   |
| 3 — Non-Goals                | 61–66   |
| 4 — Architecture             | 70–109  |
| 5 — Supported DAP Requests   | 113–180 |
| 6 — Breakpoints              | 184–217 |
| 7 — Stepping                 | 221–232 |
| 8 — Variable Inspection      | 236–250 |
| 9 — Concurrency Support      | 254–267 |
| 10 — Exception Handling      | 271–284 |
| 11 — Platform Support        | 288–294 |
| 12 — Usage                   | 298–324 |
| 13 — Editor Integration      | 328–369 |
| 14 — File Layout             | 373–450 |
| 15 — Module Responsibilities | 454–625 |
| 16 — Data Flow               | 629–658 |
| 17 — VM Instrumentation      | 662–700 |
| 18 — Local Variable Names    | 704–708 |
| 19 — Output Capture          | 712–720 |
| 20 — Error Handling          | 724–735 |
| 21 — CMake Integration       | 739–755 |
| 22 — Testing Strategy        | 759–810 |
| 23 — Future Extensions       | 814–820 |
| See Also                     | 824–830 |

## Luma_Development_Environment.md

| Section                                           | Lines   |
| ------------------------------------------------- | ------- |
| Table of Contents                                 | 5–12    |
| 1 — Overview                                      | 16–20   |
| 2 — Visual Studio Code                            | 24–90   |
| 3 — Zed                                           | 94–128  |
| 4 — Debugging the C++ Interpreter from a Terminal | 132–147 |
| 5 — Keeping Editor Configurations in Sync         | 151–159 |
| 6 — See Also                                      | 163–169 |

## Luma_Error_Handling.md

| Section                                | Lines   |
| -------------------------------------- | ------- |
| Table of Contents                      | 9–21    |
| 1 — Error Categories                   | 25–36   |
| 2 — Domain Failures — `result<T>`      | 40–131  |
| 3 — Absent Values — `optional<T>`      | 135–187 |
| 4 — Programmer Errors — Runtime Errors | 191–210 |
| 5 — Try / Catch / Finally              | 214–261 |
| 6 — Standard Library Conventions       | 265–310 |
| 7 — Third-Party Library Conventions    | 314–361 |
| 8 — Anti-Patterns                      | 365–458 |
| 9 — Interpreter Implementation Policy  | 462–517 |
| See Also                               | 521–531 |

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
| 2 — Download                          | 39–55   |
| 3 — Verify Checksums                  | 59–84   |
| 4 — Windows                           | 88–123  |
| 5 — macOS                             | 127–172 |
| 6 — Linux                             | 176–221 |
| 7 — Editor Setup — Visual Studio Code | 225–277 |
| 8 — Editor Setup — Zed                | 281–288 |
| 9 — Verify the Installation           | 292–331 |
| 10 — Building from Source             | 335–345 |
| 11 — Troubleshooting                  | 349–365 |
| See Also                              | 369–379 |

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
| 7 — Hover                       | 161–170   |
| 8 — Completion                  | 174–184   |
| 9 — Document Management         | 188–192   |
| 10 — Standard Library Support   | 196–203   |
| 11 — Platform Support           | 207–211   |
| 12 — Usage                      | 215–267   |
| 13 — Editor Integration         | 271–279   |
| 14 — File Layout                | 283–438   |
| 15 — Module Responsibilities    | 442–858   |
| 16 — Data Flow                  | 862–1038  |
| 17 — JSON-RPC Dispatch          | 1042–1085 |
| 18 — Capability Negotiation     | 1089–1136 |
| 19 — Stdlib Signature Access    | 1140–1149 |
| 20 — Build Integration          | 1153–1187 |
| 21 — Error Handling             | 1191–1198 |
| 22 — Logging                    | 1202–1211 |
| 23 — Platform-Specific Handling | 1215–1227 |
| See Also                        | 1231–1237 |

## Luma_Manual_Tests.md

| Section                              | Lines   |
| ------------------------------------ | ------- |
| Table of Contents                    | 5–18    |
| Prerequisites                        | 22–175  |
| 1 — Interpreter                      | 179–488 |
| 2 — Language Server (VS Code)        | 492–575 |
| 3 — Debugger (VS Code)               | 579–656 |
| 4 — VS Code Extension Features       | 660–701 |
| 5 — Zed Extension                    | 705–754 |
| 6 — Cross-Cutting & Regression Tests | 758–785 |
| 7 — Execution Checklist              | 789–801 |
| 8 — Reporting Issues                 | 803–811 |
| See Also                             | 815–821 |

## Luma_Performance_Guide.md

| Section                               | Lines   |
| ------------------------------------- | ------- |
| Table of Contents                     | 5–16    |
| 1 — Immutability and Deep Copies      | 20–30   |
| 2 — Collection Performance Comparison | 32–49   |
| 3 — String Building                   | 51–70   |
| 4 — Recursion Limits                  | 72–74   |
| 5 — Loop Iteration Limits             | 76–78   |
| 6 — Resource Limits                   | 80–101  |
| 7 — Interpreter Optimisations         | 103–111 |
| See Also                              | 115–120 |

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
| 7 — REPL Commands                   | 187–250 |
| 8 — Line Editing and Tab Completion | 254–294 |
| 9 — Error Handling                  | 298–343 |
| 10 — Tips and Tricks                | 347–356 |
| See Also                            | 360–366 |

## Luma_Sandbox.md

| Section                       | Lines   |
| ----------------------------- | ------- |
| Table of Contents             | 7–19    |
| 1 — Overview                  | 23–27   |
| 2 — Threat Model              | 31–53   |
| 3 — Capability Model          | 57–73   |
| 4 — Guarantees                | 77–101  |
| 5 — Non-Goals and Limitations | 105–114 |
| 6 — Enforcement               | 118–126 |
| 7 — Validation                | 130–136 |
| 8 — Adding a Module Safely    | 140–147 |
| 9 — Recommended Deployment    | 151–157 |
| See Also                      | 161–167 |

## Luma_Software_Architecture.md

| Section                                             | Lines     |
| --------------------------------------------------- | --------- |
| Table of Contents                                   | 7–29      |
| 1 — Introduction                                    | 33–52     |
| 2 — Design Goals and Constraints                    | 56–75     |
| 3 — High-Level Architecture                         | 79–111    |
| 4 — Module Decomposition                            | 115–865   |
| 5 — Data Structures                                 | 869–1046  |
| 6 — Processing Pipeline                             | 1050–1150 |
| 7 — Bytecode Compiler and Virtual Machine Internals | 1154–1879 |
| 8 — Type System Design                              | 1883–1929 |
| 9 — Memory Management Strategy                      | 1933–1963 |
| 10 — Error Handling Strategy                        | 1967–2014 |
| 11 — Standard Library Architecture                  | 2018–2152 |
| 12 — Concurrency Architecture                       | 2156–2263 |
| 13 — REPL Architecture                              | 2267–2292 |
| 14 — Testing Architecture                           | 2296–2320 |
| 15 — File Inclusion and Source Management           | 2324–2351 |
| 16 — Project File Structure                         | 2355–2425 |
| 17 — Cross-Platform Considerations                  | 2429–2455 |
| 18 — Debugger Architecture                          | 2459–2478 |
| 19 — Design Decisions and Rationale                 | 2482–2575 |
| See Also                                            | 2579–2588 |

## Luma_Standard_Library_Reference.md

| Section                | Lines     |
| ---------------------- | --------- |
| Table of Contents      | 11–52     |
| 1 — Core Built-Ins     | 56–67     |
| 2 — Array              | 69–138    |
| 3 — Bits               | 140–162   |
| 4 — Calculus           | 166–194   |
| 5 — Channel            | 196–247   |
| 6 — Compression        | 249–296   |
| 7 — Console            | 298–318   |
| 8 — Converter          | 320–343   |
| 9 — Csv                | 345–377   |
| 10 — DateTime          | 379–520   |
| 11 — Decimal           | 522–609   |
| 12 — Dictionary        | 611–652   |
| 13 — Encoder           | 654–683   |
| 14 — FileSystem        | 685–777   |
| 15 — Hash              | 779–821   |
| 16 — Http              | 823–930   |
| 17 — Json              | 932–1027  |
| 18 — KeyValueStore     | 1029–1083 |
| 19 — LinearAlgebra     | 1085–1142 |
| 20 — Log               | 1144–1170 |
| 21 — Math              | 1172–1305 |
| 22 — Optional          | 1307–1362 |
| 23 — Order             | 1364–1415 |
| 24 — Process           | 1417–1518 |
| 25 — Queue             | 1520–1545 |
| 26 — Random            | 1547–1601 |
| 27 — Reference         | 1603–1664 |
| 28 — RegularExpression | 1666–1769 |
| 29 — Resource          | 1771–1822 |
| 30 — Result            | 1824–1855 |
| 31 — Set               | 1857–1890 |
| 32 — Socket            | 1892–1958 |
| 33 — Stack             | 1962–1988 |
| 34 — Statistics        | 1990–2002 |
| 35 — String            | 2004–2116 |
| 36 — Task              | 2118–2192 |
| 37 — Terminal          | 2194–2413 |
| 38 — Xml               | 2415–2490 |
| See Also               | 2494–2501 |

## Luma_Syntax_Highlighting.md

| Section                                      | Lines   |
| -------------------------------------------- | ------- |
| Table of Contents                            | 7–36    |
| 1 — Token Design                             | 40–310  |
| 2 — Visual Studio Code Extension             | 314–524 |
| 3 — Zed Extension                            | 528–741 |
| 4 — Semantic Tokens from the Language Server | 745–767 |
| See Also                                     | 771–775 |

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
| 1 — Getting Started                              | 46–177    |
| 2 — Types                                        | 181–287   |
| 3 — Variables and Mutability                     | 291–404   |
| 4 — Operators                                    | 408–620   |
| 5 — Control Flow                                 | 624–824   |
| 6 — Functions                                    | 828–939   |
| 7 — Lambdas                                      | 943–1031  |
| 8 — Records                                      | 1035–1189 |
| 9 — Arrays                                       | 1193–1288 |
| 10 — Dictionaries                                | 1292–1352 |
| 11 — Tuples                                      | 1356–1424 |
| 12 — Choice Types — Unit Variants                | 1428–1478 |
| 13 — Choice Types (ADTs)                         | 1482–1593 |
| 14 — Result and Optional                         | 1597–1835 |
| 15 — Match                                       | 1839–2098 |
| 16 — String Interpolation and Multi-Line Strings | 2102–2164 |
| 17 — Pipe Operator                               | 2168–2213 |
| 18 — Named Arguments                             | 2217–2247 |
| 19 — Type Aliases                                | 2251–2273 |
| 20 — Interfaces                                  | 2277–2353 |
| 21 — Generics, `downcast`, and `is`              | 2357–2587 |
| 22 — Namespaces and `use`                        | 2591–2786 |
| 23 — Ownership (`unique` and `borrow`)           | 2790–2834 |
| 24 — Testing with `@test`                        | 2838–2895 |
| 25 — Including Files                             | 2899–2964 |
| 26 — Standard Library Reference                  | 2968–2972 |
| 27 — Linter and `--strict` Mode                  | 2976–3028 |
| 28 — Reserved Keywords                           | 3032–3051 |
| 29 — Error Reference                             | 3055–3422 |
| 30 — Complete Programs                           | 3426–3632 |
| 31 — Debugging                                   | 3636–3657 |
| 32 — Formal Grammar (EBNF)                       | 3661–4015 |
| See Also                                         | 4019–4031 |

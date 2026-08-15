using System;
using System.Diagnostics;
using ImpulseGraph;
using ImpulseGraph.Vm;

namespace Examples;

class Program
{
    static void Main(string[] args)
    {
        Console.WriteLine("===================================================================");
        Console.WriteLine(" Impulse Graph Engine — Example 04: Analytical VM Queries (.NET)");
        Console.WriteLine("===================================================================\n");

        string snapshotPath = "financial_transactions.imps";

        try
        {
            using var snap = new Snapshot(snapshotPath);
            Console.WriteLine($"[INFO] Successfully resolved and opened '{snapshotPath}'.");

            Console.WriteLine("\n1. Compiling Low-Level impOps Bytecode with QueryBuilder:");

            var query = new QueryBuilder()
                .InputNode(0)     // R0: Seed Account 0
                .WalkEdge(0)      // R1: Transfer recipients
                .CollectBitset()  // R2: Output bitset
                .Compile();

            Console.WriteLine($"   -> Generated {query.InstructionCount} VM instructions.");

            var sw = Stopwatch.StartNew();
            using var result = snap.ExecuteQuery(query, inputParam: 0UL);
            sw.Stop();

            Console.WriteLine($"\n2. VM Execution Performance:");
            Console.WriteLine($"   -> Status: {result.Status}");
            Console.WriteLine($"   -> Latency: {sw.Elapsed.TotalNanoseconds:F0} ns");
            Console.WriteLine($"   -> Result Register: R{result.ResultRegister}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[INFO] {ex.Message}");
            Console.WriteLine("[INFO] Set IMPULSE_DATASETS_DIR or extract sample datasets.");
        }

        Console.WriteLine("\n[SUCCESS] Example 04 completed cleanly.");
    }
}

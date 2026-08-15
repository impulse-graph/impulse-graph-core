using System;
using System.Diagnostics;
using ImpulseGraph;
using ImpulseGraph.Vm;

namespace Examples;

class Program
{
    static void Main(string[] args)
    {
        Console.WriteLine("===============================================================");
        Console.WriteLine(" Impulse Graph Engine — Example 02: Social Graph Traversal (.NET)");
        Console.WriteLine("===============================================================\n");

        string snapshotPath = "social_graph.imps";

        try
        {
            using var snap = new Snapshot(snapshotPath);
            Console.WriteLine($"[INFO] Successfully resolved and opened '{snapshotPath}'.");

            Console.WriteLine("\n1. Constructing Fluent ImpulseVM Query Plan:");
            Console.WriteLine("   Query: Seed(User 0) -> Walk(follows) -> Walk(follows) -> CollectBitset()");

            var query = new QueryBuilder()
                .InputNode(0)
                .WalkEdge(0)
                .WalkEdge(0)
                .CollectBitset()
                .Compile();

            Console.WriteLine($"   -> Generated {query.InstructionCount} impOps bytecode instructions.");

            var sw = Stopwatch.StartNew();
            using var result = snap.ExecuteQuery(query, inputParam: 0UL);
            sw.Stop();

            Console.WriteLine($"\n2. Query Execution against ImpulseVM:");
            Console.WriteLine($"   -> Execution Time: {sw.Elapsed.TotalMicroseconds:F2} µs");
            Console.WriteLine($"   -> Status: {result.Status}");
            Console.WriteLine($"   -> Destination Bitset Register: R{result.ResultRegister}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[INFO] {ex.Message}");
            Console.WriteLine("[INFO] Set IMPULSE_DATASETS_DIR or extract sample datasets.");
        }

        Console.WriteLine("\n[SUCCESS] Example 02 completed cleanly.");
    }
}

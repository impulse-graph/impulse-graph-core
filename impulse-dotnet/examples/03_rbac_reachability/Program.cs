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
        Console.WriteLine(" Impulse Graph Engine — Example 03: ReBAC Authorization (.NET)");
        Console.WriteLine("===============================================================\n");

        string snapshotPath = "rbac_snapshot.imps";

        try
        {
            using var snap = new Snapshot(snapshotPath);
            Console.WriteLine($"[INFO] Successfully resolved and opened '{snapshotPath}'.");

            Console.WriteLine("\n1. ReBAC Policy: Check permissions for User 0 (User -> Role -> Permission):");

            var query = new QueryBuilder()
                .InputNode(0)    // Seed User 0
                .WalkEdge(0)     // Walk User -> Role
                .WalkEdge(1)     // Walk Role -> Permission
                .CollectBitset() // Collect Permission Bitset
                .Compile();

            var sw = Stopwatch.StartNew();
            using var result = snap.ExecuteQuery(query, inputParam: 0UL);
            sw.Stop();

            Console.WriteLine($"   -> ReBAC Evaluation Latency: {sw.Elapsed.TotalNanoseconds:F0} ns");
            Console.WriteLine($"   -> Result Status: {result.Status}");
            Console.WriteLine($"   -> Permissions Bitset Register: R{result.ResultRegister}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[INFO] {ex.Message}");
            Console.WriteLine("[INFO] Set IMPULSE_DATASETS_DIR or extract sample datasets.");
        }

        Console.WriteLine("\n[SUCCESS] Example 03 completed cleanly.");
    }
}

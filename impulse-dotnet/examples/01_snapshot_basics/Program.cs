using System;
using ImpulseGraph;

namespace Examples;

class Program
{
    static void Main(string[] args)
    {
        Console.WriteLine("===============================================================");
        Console.WriteLine(" Impulse Graph Engine — Example 01: Snapshot Basics (.NET)");
        Console.WriteLine("===============================================================\n");

        string snapshotPath = "sample_basics.imps";

        Console.WriteLine($"1. Opening snapshot: {snapshotPath} via zero-copy mmap...");
        try
        {
            using var snap = new Snapshot(snapshotPath);

            Console.WriteLine($"   -> Magic:     0x{snap.Magic:X} ('IMPS')");
            Console.WriteLine($"   -> Version:   {snap.Version}");
            Console.WriteLine($"   -> Domains:   {snap.DomainCount}");
            Console.WriteLine($"   -> Relations: {snap.RelationCount}\n");

            Console.WriteLine("2. Direct Point Reachability Queries:");
            Console.WriteLine($"   -> Node 0 -> Node 1 reachable? {snap.IsReachable(0, 0, 1)}");
            Console.WriteLine($"   -> Node 0 -> Node 3 reachable? {snap.IsReachable(0, 0, 3)}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[INFO] {ex.Message}");
            Console.WriteLine("[INFO] Set IMPULSE_DATASETS_DIR or extract sample datasets.");
        }

        Console.WriteLine("\n[SUCCESS] Example 01 completed cleanly.");
    }
}

using System;
using System.IO;
using ImpulseGraph;
using ImpulseGraph.Native;
using ImpulseGraph.Vm;
using Xunit;

namespace ImpulseGraph.Tests;

public class SnapshotTests
{
    [Fact]
    public void TestQueryBuilderCompilation()
    {
        var qb = new QueryBuilder();
        qb.InputNode(0)
          .WalkEdge(0)
          .CollectBitset();

        var query = qb.Compile();
        Assert.NotNull(query);
        Assert.True(query.InstructionCount >= 3);
        Assert.NotEmpty(query.Bytecode);
    }

    [Fact]
    public void TestInvalidPathThrows()
    {
        Assert.Throws<InvalidOperationException>(() =>
        {
            using var snap = new Snapshot("__non_existent_path_12345.imps");
        });
    }
}

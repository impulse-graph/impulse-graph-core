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
    public void TestQueryBuilderOpcodesAndPayloads()
    {
        var qb = new QueryBuilder();
        var query = qb
            .InputNode(0)
            .InputSet(1)
            .LoadConstInt(7, 2)
            .WalkEdge(3, 4)
            .WalkEdgeFiltered(3, 5, 6)
            .Degree(3, 7)
            .CscWalk(3, 8)
            .SetDifference(1, 2, 9)
            .MxV(3, 4, 10, semiringId: 2)
            .RebacCheck(42, 11)
            .CollectBitset(11)
            .CollectArray(11)
            .Compile();

        var span = query.AsSpan();
        Assert.Equal(0x02, span[0].Opcode); // InputNode
        Assert.Equal(0x03, span[1].Opcode); // InputSet
        Assert.Equal(0x04, span[2].Opcode); // LoadConstInt
        Assert.Equal(0x10, span[3].Opcode); // WalkEdge
        Assert.Equal((3u << 16) | 2u, span[3].Payload);
        Assert.Equal(0x11, span[4].Opcode); // WalkEdgeFiltered
        Assert.Equal((3u << 16) | (5u << 8) | 4u, span[4].Payload);
        Assert.Equal(0x12, span[5].Opcode); // Degree
        Assert.Equal((3u << 16) | 6u, span[5].Payload);
        Assert.Equal(0x18, span[6].Opcode); // CscWalk
        Assert.Equal((3u << 16) | 7u, span[6].Payload);
        Assert.Equal(0x32, span[7].Opcode); // SetDifference
        Assert.Equal((2u << 16) | 1u, span[7].Payload);
        Assert.Equal(0x41, span[8].Opcode); // MxV
        Assert.Equal((2u << 16) | (3u << 8) | 4u, span[8].Payload);
        Assert.Equal(0x63, span[9].Opcode); // RebacCheck
        Assert.Equal(42u, span[9].Payload);
        Assert.Equal(0x90, span[10].Opcode); // CollectBitset
        Assert.Equal(11u, span[10].Payload);
        Assert.Equal(0x91, span[11].Opcode); // CollectArray
        Assert.Equal(11u, span[11].Payload);
        Assert.Equal(0x00, span[12].Opcode); // Halt
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

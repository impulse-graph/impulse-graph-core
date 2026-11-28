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

    [Fact]
    public void TestPhase1NativeStructSizes()
    {
        Assert.Equal(32, System.Runtime.InteropServices.Marshal.SizeOf<ImpulseDomainEntry>());
        Assert.Equal(192, System.Runtime.InteropServices.Marshal.SizeOf<ImpulseRelationEntry>());
        Assert.Equal(44, System.Runtime.InteropServices.Marshal.SizeOf<ImpulseAttributeDescriptor>());
        Assert.Equal(64, System.Runtime.InteropServices.Marshal.SizeOf<ImpulseIndexEntry>());
        Assert.Equal(80, System.Runtime.InteropServices.Marshal.SizeOf<ImpulseSnapshotHeaderInfo>());
    }

    [Fact]
    public void TestAttributeInfoValidityRangeAnd64BitOffsets()
    {
        var nullable = new AttributeInfo(
            "score", 0x86, 0x06, true, false, 1, null, null, 4096UL, 256UL, 0UL, 0UL);
        Assert.Equal((0UL, 0UL), nullable.GetValidityRange(0UL));
        Assert.Equal((4096UL, 128UL), nullable.GetValidityRange(4UL));
        Assert.Equal((4096UL, 128UL), nullable.GetValidityRange(1024UL));
        Assert.Equal((4096UL, 256UL), nullable.GetValidityRange(1025UL));
        Assert.Equal((4096UL, 256UL), nullable.GetValidityRange(ulong.MaxValue));

        var nonNullable = nullable with { TypeCode = 0x06, IsNullable = false };
        Assert.Equal((0UL, 0UL), nonNullable.GetValidityRange(4UL));

        var varStr64 = new AttributeInfo(
            "bio", ImpulseAttrTypes.AttrTypeVarString, ImpulseAttrTypes.AttrTypeVarString,
            false, true, 1, null, null, 4096UL, 128UL, 4224UL, 128UL);
        Assert.True(varStr64.Has64BitOffsets);
    }

    [Fact]
    public void TestMetadataIntrospectionRoundtrip()
    {
        string? fixturePath = FindTestVector("tc04_dense_single_relation");
        Assert.True(fixturePath is not null, "tc04_dense_single_relation/snapshot.imps fixture not found");

        using var snap = new Snapshot(fixturePath!);
        Assert.True(snap.IsOpen);
        Assert.Equal(0x494D5053u, snap.Magic);
        Assert.True(snap.Version >= 9);

        var hdr = snap.GetHeader();
        Assert.Equal(snap.Magic, hdr.Magic);
        Assert.Equal(snap.Version, hdr.Version);
        Assert.Equal(snap.FileSize, hdr.FileSize);
        Assert.Equal(16, hdr.SnapshotUuid.Length);
        Assert.Equal((ulong)new FileInfo(fixturePath!).Length, snap.FileSize);

        var sha = snap.ComputeSha256();
        Assert.Equal(32, sha.Length);
        using (var fs = File.OpenRead(fixturePath!))
        {
            var expectedSha = System.Security.Cryptography.SHA256.HashData(fs);
            Assert.Equal(expectedSha, sha);
        }

        var domains = snap.Domains;
        Assert.Equal(snap.DomainCount, domains.Count);
        for (ushort di = 0; di < domains.Count; di++)
        {
            var d = domains[di];
            Assert.Equal(d.DomainId, snap.GetDomainById(d.DomainId)?.DomainId);
            if (!string.IsNullOrEmpty(d.Name))
            {
                Assert.Equal(d.Name, snap.GetDomainByName(d.Name)?.Name);
            }
            Assert.Equal(d.AttrCount, snap.GetDomainAttributeCount(di));
        }
        Assert.Null(snap.GetDomainByName("__missing__"));
        Assert.Null(snap.GetDomainById(ushort.MaxValue));

        var relations = snap.Relations;
        Assert.Equal(snap.RelationCount, relations.Count);
        for (ushort ri = 0; ri < relations.Count; ri++)
        {
            var r = relations[ri];
            Assert.Equal(r.RelationId, snap.GetRelationById(r.RelationId)?.RelationId);
            Assert.Equal(r.Name, snap.GetRelationName(ri));
            Assert.Equal(r.VirtualComponentIds, snap.GetRelationVirtualComponents(ri));
            if (!string.IsNullOrEmpty(r.Name))
            {
                Assert.Equal(r.Name, snap.GetRelationByName(r.Name)?.Name);
            }
            Assert.True(r.SrcNodeIdWidth is 2 or 4 or 8);
            Assert.True(r.TgtNodeIdWidth is 2 or 4 or 8);
            Assert.NotNull(r.VirtualComponentIds);
        }
        Assert.Null(snap.GetRelationByName("__missing__"));
        Assert.Null(snap.GetRelationById(ushort.MaxValue));

        var indices = snap.Indices;
        Assert.Equal(snap.IndexCount, indices.Count);

        var allMeta = snap.GetAllMetadata();
        Assert.Equal((int)snap.MetadataCount, allMeta.Count);
        foreach (var kv in allMeta)
        {
            Assert.Equal(kv.Value, snap.GetMetadata(kv.Key));
        }
        Assert.Null(snap.GetMetadata("__missing__"));
        Assert.Throws<ArgumentNullException>(() => snap.GetMetadata(null!));

        // Out-of-bounds and post-Dispose error contracts
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetDomainEntry(ushort.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetDomainAttributeInfo(0, ushort.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetAttributeInfo(0, ushort.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetRelationInfo(ushort.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetRelationName(ushort.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetRelationVirtualComponents(ushort.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => snap.GetIndexEntry(ushort.MaxValue));
        snap.Dispose();
        Assert.False(snap.IsOpen);
        Assert.Throws<ObjectDisposedException>(() => snap.GetHeader());
        Assert.Throws<ObjectDisposedException>(() => snap.ComputeSha256());
        Assert.Throws<ObjectDisposedException>(() => _ = snap.Domains);
        Assert.Throws<ObjectDisposedException>(() => _ = snap.Relations);
        Assert.Throws<ObjectDisposedException>(() => _ = snap.Indices);
        Assert.Throws<ObjectDisposedException>(() => snap.GetDomainEntry(0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetDomainAttributeInfo(0, 0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetAttributeInfo(0, 0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetRelationInfo(0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetRelationName(0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetRelationVirtualComponents(0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetIndexEntry(0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetDomainById(0));
        Assert.Throws<ObjectDisposedException>(() => snap.GetRelationByName("any"));
        Assert.Throws<ObjectDisposedException>(() => snap.GetAllMetadata());
        Assert.Throws<ObjectDisposedException>(() => snap.GetMetadata("any"));
    }

    [Fact]
    public void TestEmptyAndV09RawUint16Fixtures()
    {
        string? emptyPath = FindTestVector("tc03_empty_snapshot");
        Assert.True(emptyPath is not null, "tc03_empty_snapshot/snapshot.imps fixture not found");
        using (var snap = new Snapshot(emptyPath!))
        {
            Assert.Equal(0, snap.DomainCount);
            Assert.Equal(0, snap.RelationCount);
            Assert.Equal(0, snap.IndexCount);
            Assert.Equal(0u, snap.MetadataCount);
            Assert.Empty(snap.Domains);
            Assert.Empty(snap.Relations);
            Assert.Empty(snap.Indices);
            Assert.Empty(snap.GetAllMetadata());
        }

        string? raw16Path = FindTestVector("tc09_encoding_raw_uint16");
        Assert.True(raw16Path is not null, "tc09_encoding_raw_uint16/snapshot.imps fixture not found");
        using (var snap = new Snapshot(raw16Path!))
        {
            Assert.True(snap.RelationCount > 0);
            var rel = snap.GetRelationInfo(0);
            Assert.False(rel.IsVirtual);
            Assert.True(rel.HasCsr);
            Assert.Equal(rel.AttrCount, rel.AttrCount);
            for (ushort ai = 0; ai < rel.AttrCount; ai++)
            {
                var attr = snap.GetAttributeInfo(0, ai);
                Assert.NotNull(attr.Name);
            }
        }
    }

    private static string? FindTestVector(string vectorName)
    {
        string[] roots = { Directory.GetCurrentDirectory(), AppContext.BaseDirectory };
        foreach (var start in roots)
        {
            var dir = new DirectoryInfo(start);
            for (int depth = 0; depth < 8 && dir is not null; depth++, dir = dir.Parent)
            {
                string[] relPaths =
                {
                    Path.Combine(dir.FullName, "spec", "test-vectors", vectorName, "snapshot.imps"),
                    Path.Combine(dir.FullName, "impulse-graph-spec", "test-vectors", vectorName, "snapshot.imps"),
                };
                foreach (var p in relPaths)
                {
                    if (File.Exists(p)) return p;
                }
            }
        }
        return null;
    }
}

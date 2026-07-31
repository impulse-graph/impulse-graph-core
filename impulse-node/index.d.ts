export class Snapshot {
  constructor(path: string);
  isReachable(srcDomain: number, srcId: number, tgtDomain: number, tgtId: number): boolean;
}

export class Writer {
  constructor(outputPath: string, globalFeatures?: number);
  addDomain(domainId: number, keyType: number, name: string): void;
  addRelation(
    srcDomainId: number,
    tgtDomainId: number,
    encodingType: number,
    nodeCount: number,
    edgeCount: number,
    sectionFeatures: number,
    rowOffsets: Uint32Array,
    colIndicesBytes: Buffer
  ): void;
  finalize(): void;
}

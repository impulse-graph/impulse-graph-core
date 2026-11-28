export class Decimal64 {
  readonly unscaled: bigint;
  readonly precision: number;
  readonly scale: number;

  constructor(unscaled: bigint | number, precision?: number, scale?: number);
  static fromUnscaled(unscaled: bigint | number, precision?: number, scale?: number): Decimal64;
  static fromString(str: string, precision?: number, scale?: number): Decimal64;
  toString(): string;
  toNumber(): number;
  equals(other: unknown): boolean;
}

export interface SnapshotHeaderInfo {
  magic: number;
  version: number;
  dataOffset: number;
  domainCount: number;
  relationCount: number;
  indexCount: number;
  timestampMs: bigint;
  requiredFeatures: bigint;
  footerDirectoryOffset: bigint;
  footerDirectoryBytes: bigint;
  fileSize: bigint;
  headerChecksum: number;
  snapshotUuid: Buffer;
}

export interface DomainInfo {
  domainId: number;
  keyType: number;
  denseIdWidth: number;
  nodeCount: bigint;
  attrCount: number;
  keyAttrIndex: number;
  name: string;
}

export interface RelationInfo {
  relationId: number;
  name: string;
  srcDomainId: number;
  tgtDomainId: number;
  encodingId: number;
  nodeIdWidth: number;
  edgeIndexWidth: number;
  nodeCount: bigint;
  edgeCount: bigint;
  sectionFeatures: bigint;
  attrCount: number;
  csrRowOffOffset: bigint;
  csrRowOffBytes: bigint;
  csrColIdxOffset: bigint;
  csrColIdxBytes: bigint;
  cscRowOffOffset: bigint;
  cscRowOffBytes: bigint;
  cscColIdxOffset: bigint;
  cscColIdxBytes: bigint;
  cooSrcOffset: bigint;
  cooSrcBytes: bigint;
  cooTgtOffset: bigint;
  cooTgtBytes: bigint;
  cooEdgeIdOffset: bigint;
  cooEdgeIdBytes: bigint;
  hasCsc: boolean;
  hasCoo: boolean;
  cooOrder: number;
  isVirtual: boolean;
  virtualComponentIds: number[];
}

export interface IndexInfo {
  indexId: number;
  domainId: number;
  relationId: number;
  attributeIndex: number;
  indexType: number;
  name: string;
  dataOffset: bigint;
  dataBytes: bigint;
  payloadFeatureMask: bigint;
}

export interface AttributeInfo {
  name: string;
  typeCode: number;
  baseTypeCode: number;
  isNullable: boolean;
  dimension: number;
  dataOffset: bigint;
  dataBytes: bigint;
  offsetsOffset: bigint;
  offsetsBytes: bigint;
  precision?: number;
  scale?: number;
}

export interface RawDecimal64Column {
  precision: number;
  scale: number;
  isNullable: boolean;
  dimension: number;
  rowCount: number;
  unscaled: BigInt64Array;
  validBits: BigUint64Array | null;
}

export interface Decimal64WriteOptions {
  nullable?: boolean;
  dimension?: number;
}

export interface V11ParamSchema {
  name: string;
  paramId: number;
  type: string;
  isDecimal: boolean;
  precision: number;
  scale: number;
  domainId: number;
}

export interface V11OutputSchema {
  name: string;
  type: string;
  isDecimal: boolean;
  precision: number;
  scale: number;
}

export class CompiledV11Program {
  readonly impbBytes: Buffer;
  readonly paramNameToId: Record<string, number>;
  readonly paramSchema: V11ParamSchema[];
  readonly outputNames: string[];
  readonly outputSchema: V11OutputSchema[];
  readonly impscmSource: string | null;

  execute(
    snapshot?: Snapshot | null,
    params?: Record<string, unknown>,
    scratchBytes?: number
  ): Record<string, unknown>;
}

export type CypherParamTypeSpec =
  | string
  | {
      type: string;
      precision?: number;
      scale?: number;
      domain?: string;
    };

export function compileImpScm(
  source: string,
  snapshot?: Snapshot | null,
  enableOptimizations?: boolean
): CompiledV11Program;

export function compileCypher(
  cypher: string,
  snapshot?: Snapshot | null,
  paramTypes?: Record<string, CypherParamTypeSpec>,
  enableOptimizations?: boolean
): CompiledV11Program;

export class Snapshot {
  constructor(path: string);
  header(): SnapshotHeaderInfo;
  fileSize(): bigint;
  sha256(): string;
  domainCount(): number;
  getDomain(domainIndex: number): DomainInfo;
  relationCount(): number;
  getRelation(relationIndex: number): RelationInfo;
  getRelationName(relationIndex: number): string | null;
  getDomainAttributeCount(domainIndex: number): number;
  getDomainAttributeInfo(domainIndex: number, attrIndex: number): AttributeInfo;
  getAttributeInfo(relationIndex: number, attrIndex: number): AttributeInfo;
  getDomainAttributeDecimal64(domainIndex: number, attrIndex: number): RawDecimal64Column;
  getAttributeDecimal64(relationIndex: number, attrIndex: number): RawDecimal64Column;
  getDomainDecimal64(domainIndex: number, attrIndex: number): Array<Decimal64 | null>;
  getRelationDecimal64(relationIndex: number, attrIndex: number): Array<Decimal64 | null>;
  indexCount(): number;
  getIndex(indexIdx: number): IndexInfo;
  getMetadata(key: string): string | null;
  getAllMetadata(): Record<string, string>;
  isReachable(
    relationIndex: number,
    srcId: number | bigint,
    tgtId: number | bigint
  ): boolean;
  isReachable(
    srcDomain: number,
    srcId: number | bigint,
    tgtDomain: number,
    tgtId: number | bigint
  ): boolean;
  executeCypher(cypher: string, params?: Record<string, unknown>): number[];
  close(): void;
}

export class Writer {
  constructor(outputPath: string, globalFeatures?: number);
  addDomain(domainId: number, keyType: number, name: string): void;
  setDomainNodeCount(domainIndex: number, nodeCount: number | bigint): void;
  addDomainAttributeDecimal64(
    domainIndex: number,
    name: string,
    precision: number,
    scale: number,
    data: Buffer | ArrayBufferView,
    dimension?: number,
    typeCode?: number
  ): void;
  addDomainDecimal64(
    domainIndex: number,
    name: string,
    precision: number,
    scale: number,
    values: Array<Decimal64 | string | bigint | number | null> | Buffer | ArrayBufferView,
    options?: Decimal64WriteOptions
  ): void;
  addRelation(
    srcDomainId: number,
    tgtDomainId: number,
    encodingType: number,
    nodeCount: number | bigint,
    edgeCount: number | bigint,
    sectionFeatures: number | bigint,
    rowOffsets: Uint32Array,
    colIndicesBytes: Buffer | Uint32Array
  ): void;
  addAttributeDecimal64(
    relationIndex: number,
    name: string,
    precision: number,
    scale: number,
    data: Buffer | ArrayBufferView,
    dimension?: number,
    typeCode?: number
  ): void;
  addRelationDecimal64(
    relationIndex: number,
    name: string,
    precision: number,
    scale: number,
    values: Array<Decimal64 | string | bigint | number | null> | Buffer | ArrayBufferView,
    options?: Decimal64WriteOptions
  ): void;
  setMetadata(key: string, value: string): void;
  finalize(): void;
}

export interface VmWorkspaceOutput {
  slot: number;
  elemTag: number;
  length: number;
  data?: Array<number | boolean | null>;
  i64Data?: Array<bigint | null>;
}

export class VmWorkspace {
  constructor(scratchBytes?: number);
  destroy(): void;
  execute(
    impb: Buffer,
    snapshot: Snapshot | null,
    seedIds?: Array<number | bigint>,
    domainId?: number
  ): number;
  executeParams(
    impb: Buffer,
    snapshot?: Snapshot | null,
    bindings?: Array<Record<string, unknown>>,
    gasLimit?: number | bigint
  ): number;
  outputCount(): number;
  getOutput(slot: number): VmWorkspaceOutput | null;
  computeIctxV2(): { logicalHash: bigint; physicalHash: bigint };
}

export const Opcodes: Record<string, number> & {
  SCALAR_DECIMAL_ALU: 0x15;
  VEC_DECIMAL_ALU: 0x58;
};
export const VmStatus: Record<string, number> & {
  OK: 0;
  ARITHMETIC_OVERFLOW: 16;
  DIV_BY_ZERO: 17;
  ERR_ARITHMETIC_OVERFLOW: 16;
  ERR_DIV_BY_ZERO: 17;
};
export const AttrType: Record<string, number> & {
  BOOL: 0x0f;
  DECIMAL64: 0x15;
  DECIMAL64_NULLABLE: 0x95;
};

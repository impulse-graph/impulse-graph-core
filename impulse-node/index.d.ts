export class Snapshot {
  constructor(path: string);
  isReachable(srcDomain: number, srcId: number | bigint, tgtDomain: number, tgtId: number | bigint): boolean;
  close(): void;
}

export class Writer {
  constructor(outputPath: string, globalFeatures?: number);
  addDomain(domainId: number, keyType: number, name: string): void;
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
  finalize(): void;
}

export class VmContext {
  constructor(snapshot?: Snapshot);
  destroy(): void;
  vectorSize(): number;
  acquireBitset(): number;
  releaseBitset(handle: number): void;
  bitsetAdd(handle: number, nodeId: number | bigint): void;
  bitsetTest(handle: number, nodeId: number | bigint): boolean;
  bitsetFill(handle: number, count: number | bigint): void;
  bitsetGetWord(handle: number, wordIdx: number): bigint;
  acquireFloatVector(): number;
  releaseFloatVector(handle: number): void;
  floatVectorSet(handle: number, index: number, val: number): void;
  getFloatVector(handle: number): Float32Array;
  acquireDoubleVector(): number;
  releaseDoubleVector(handle: number): void;
  doubleVectorSet(handle: number, index: number, val: number): void;
  getDoubleVector(handle: number): Float64Array;
  acquireNodeVector(): number;
  releaseNodeVector(handle: number): void;
  getNodeVector(handle: number): BigUint64Array;
  acquireStringVector(): number;
  releaseStringVector(handle: number): void;
  stringVectorAdd(handle: number, str: string): void;
  stringVectorSize(handle: number): number;
  stringVectorGet(handle: number, index: number): string | null;
  acquireValueMap(): number;
  releaseValueMap(handle: number): void;
  valueMapSize(handle: number): number;
  valueMapGetKey(handle: number, index: number): string | null;
  valueMapGetValue(handle: number, index: number): number;
}

export class VmState {
  constructor();
  getRegister(reg: number): bigint;
  setRegister(reg: number, val: number | bigint): void;
  getRegisterType(reg: number): number;
  getPc(): number;
  getFlags(): bigint;
}

export class QueryResult {
  readonly status: number;
  readonly resultRegister: number;
  readonly resultType: number;
  readonly rawValue: bigint;
  isOk(): boolean;
  asInt(): bigint;
  asFloat(): number;
  asDouble(): number;
  testBitset(ctx: VmContext, nodeId: number | bigint): boolean;
}

export class CompiledQuery {
  execute(snapshot: Snapshot, inputParam?: number | bigint): QueryResult;
  executeWithContext(ctx: VmContext, state: VmState, inputParam?: number | bigint): QueryResult;
  instructionCount(): number;
  resultRegister(): number;
  bytecode(): Buffer;
}

export class QueryBuilder {
  constructor(startRegister?: number);
  inputNode(dstReg?: number): this;
  inputSet(dstReg?: number): this;
  loadConstInt(value: number | bigint, dstReg?: number): this;
  loadConstFloat(value: number, dstReg?: number): this;
  loadConstStrPrefix(prefix: string, dstReg?: number): this;
  loadKeys(keys: string[], dstReg?: number): this;

  walkEdge(relationId: number, flags?: number): this;
  walkEdgeFiltered(relationId: number, filterId: number): this;
  walkEdgePredicate(relationId: number, filterId: number): this;
  walkDegree(relationId: number): this;
  walkReduceSum(relationId: number, valReg: number): this;
  walkCsc(relationId: number): this;
  filterNode(filterId: number): this;
  filterNodeStrPrefix(prefix: string): this;

  unionWith(srcReg: number): this;
  intersectWith(srcReg: number): this;
  differenceWith(srcReg: number): this;
  cardinality(): this;
  vectorMulAttr(attrReg: number): this;
  vectorReduceSum(): this;
  vectorDiv(denomReg: number): this;
  l1NormDiff(otherReg: number): this;

  matrixVectorMul(matrixReg: number, semiringId?: number): this;
  vectorMatrixMul(matrixReg: number, semiringId?: number): this;
  ewiseAdd(otherReg: number, binaryOp?: number): this;
  ewiseMult(otherReg: number, binaryOp?: number): this;
  reduce(binaryOp?: number): this;

  afforest(): this;
  tcSweepBatch(): this;
  brandesForward(): this;
  brandesBackward(): this;
  deltaStepRelax(weightReg: number): this;
  sampleNeighbors(relationId: number, kSamples: number, seed?: number): this;
  randomWalk(relationId: number, steps: number, seed?: number): this;
  scatterGather(): this;
  rebacCheck(permissionId: number): this;
  roaringBitmapAnd(otherReg: number): this;
  islandDetect(secondaryReg: number): this;
  sparseMatVec(): this;
  louvainModularity(): this;
  kcoreDecomposition(): this;
  motifMatch3(): this;
  graphIsomorphism(): this;

  mov(dstReg: number, srcReg: number): this;
  clearReg(reg: number): this;
  nop(): this;
  jmp(instructionOffset: number): this;
  jz(instructionOffset: number): this;
  jnz(instructionOffset: number): this;

  repeat(count: number, body: (builder: this) => void): this;
  repeatUntilStable(body: (builder: this) => void): this;

  collectBitset(): this;
  collectArray(): this;
  mapDenseToKeys(): this;
  collectValueMap(): this;

  compile(): CompiledQuery;
}

export function executeBytecode(
  snapshot: Snapshot,
  bytecode: Buffer | Uint8Array,
  inputParam?: number | bigint
): QueryResult;

export const Opcodes: Record<string, number>;
export const RegisterType: Record<string, number>;
export const VmStatus: Record<string, number>;

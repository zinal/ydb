# Generated by devtools/yamaker.

LIBRARY()

LICENSE(
    Apache-2.0 WITH LLVM-exception AND
    NCSA
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/llvm12
    contrib/libs/llvm12/include
    contrib/libs/llvm12/lib/Analysis
    contrib/libs/llvm12/lib/Bitcode/Reader
    contrib/libs/llvm12/lib/Bitcode/Writer
    contrib/libs/llvm12/lib/IR
    contrib/libs/llvm12/lib/MC
    contrib/libs/llvm12/lib/ProfileData
    contrib/libs/llvm12/lib/Support
    contrib/libs/llvm12/lib/Target
    contrib/libs/llvm12/lib/Transforms/Scalar
    contrib/libs/llvm12/lib/Transforms/Utils
)

ADDINCL(
    contrib/libs/llvm12/lib/CodeGen
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    AggressiveAntiDepBreaker.cpp
    AllocationOrder.cpp
    Analysis.cpp
    AtomicExpandPass.cpp
    BasicBlockSections.cpp
    BasicTargetTransformInfo.cpp
    BranchFolding.cpp
    BranchRelaxation.cpp
    BreakFalseDeps.cpp
    BuiltinGCs.cpp
    CFGuardLongjmp.cpp
    CFIInstrInserter.cpp
    CalcSpillWeights.cpp
    CallingConvLower.cpp
    CodeGen.cpp
    CodeGenPassBuilder.cpp
    CodeGenPrepare.cpp
    CommandFlags.cpp
    CriticalAntiDepBreaker.cpp
    DFAPacketizer.cpp
    DeadMachineInstructionElim.cpp
    DetectDeadLanes.cpp
    DwarfEHPrepare.cpp
    EarlyIfConversion.cpp
    EdgeBundles.cpp
    ExecutionDomainFix.cpp
    ExpandMemCmp.cpp
    ExpandPostRAPseudos.cpp
    ExpandReductions.cpp
    FEntryInserter.cpp
    FaultMaps.cpp
    FinalizeISel.cpp
    FixupStatepointCallerSaved.cpp
    FuncletLayout.cpp
    GCMetadata.cpp
    GCMetadataPrinter.cpp
    GCRootLowering.cpp
    GCStrategy.cpp
    GlobalMerge.cpp
    HardwareLoops.cpp
    IfConversion.cpp
    ImplicitNullChecks.cpp
    IndirectBrExpandPass.cpp
    InlineSpiller.cpp
    InterferenceCache.cpp
    InterleavedAccessPass.cpp
    InterleavedLoadCombinePass.cpp
    IntrinsicLowering.cpp
    LLVMTargetMachine.cpp
    LatencyPriorityQueue.cpp
    LazyMachineBlockFrequencyInfo.cpp
    LexicalScopes.cpp
    LiveDebugValues/InstrRefBasedImpl.cpp
    LiveDebugValues/LiveDebugValues.cpp
    LiveDebugValues/VarLocBasedImpl.cpp
    LiveDebugVariables.cpp
    LiveInterval.cpp
    LiveIntervalCalc.cpp
    LiveIntervalUnion.cpp
    LiveIntervals.cpp
    LivePhysRegs.cpp
    LiveRangeCalc.cpp
    LiveRangeEdit.cpp
    LiveRangeShrink.cpp
    LiveRegMatrix.cpp
    LiveRegUnits.cpp
    LiveStacks.cpp
    LiveVariables.cpp
    LocalStackSlotAllocation.cpp
    LoopTraversal.cpp
    LowLevelType.cpp
    LowerEmuTLS.cpp
    MBFIWrapper.cpp
    MIRCanonicalizerPass.cpp
    MIRNamerPass.cpp
    MIRPrinter.cpp
    MIRPrintingPass.cpp
    MIRVRegNamerUtils.cpp
    MachineBasicBlock.cpp
    MachineBlockFrequencyInfo.cpp
    MachineBlockPlacement.cpp
    MachineBranchProbabilityInfo.cpp
    MachineCSE.cpp
    MachineCheckDebugify.cpp
    MachineCombiner.cpp
    MachineCopyPropagation.cpp
    MachineDebugify.cpp
    MachineDominanceFrontier.cpp
    MachineDominators.cpp
    MachineFrameInfo.cpp
    MachineFunction.cpp
    MachineFunctionPass.cpp
    MachineFunctionPrinterPass.cpp
    MachineFunctionSplitter.cpp
    MachineInstr.cpp
    MachineInstrBundle.cpp
    MachineLICM.cpp
    MachineLoopInfo.cpp
    MachineLoopUtils.cpp
    MachineModuleInfo.cpp
    MachineModuleInfoImpls.cpp
    MachineOperand.cpp
    MachineOptimizationRemarkEmitter.cpp
    MachineOutliner.cpp
    MachinePassManager.cpp
    MachinePipeliner.cpp
    MachinePostDominators.cpp
    MachineRegionInfo.cpp
    MachineRegisterInfo.cpp
    MachineSSAUpdater.cpp
    MachineScheduler.cpp
    MachineSink.cpp
    MachineSizeOpts.cpp
    MachineStableHash.cpp
    MachineStripDebug.cpp
    MachineTraceMetrics.cpp
    MachineVerifier.cpp
    MacroFusion.cpp
    ModuloSchedule.cpp
    MultiHazardRecognizer.cpp
    NonRelocatableStringpool.cpp
    OptimizePHIs.cpp
    PHIElimination.cpp
    PHIEliminationUtils.cpp
    ParallelCG.cpp
    PatchableFunction.cpp
    PeepholeOptimizer.cpp
    PostRAHazardRecognizer.cpp
    PostRASchedulerList.cpp
    PreISelIntrinsicLowering.cpp
    ProcessImplicitDefs.cpp
    PrologEpilogInserter.cpp
    PseudoProbeInserter.cpp
    PseudoSourceValue.cpp
    RDFGraph.cpp
    RDFLiveness.cpp
    RDFRegisters.cpp
    ReachingDefAnalysis.cpp
    RegAllocBase.cpp
    RegAllocBasic.cpp
    RegAllocFast.cpp
    RegAllocGreedy.cpp
    RegAllocPBQP.cpp
    RegUsageInfoCollector.cpp
    RegUsageInfoPropagate.cpp
    RegisterClassInfo.cpp
    RegisterCoalescer.cpp
    RegisterPressure.cpp
    RegisterScavenging.cpp
    RegisterUsageInfo.cpp
    RenameIndependentSubregs.cpp
    ResetMachineFunctionPass.cpp
    SafeStack.cpp
    SafeStackLayout.cpp
    ScheduleDAG.cpp
    ScheduleDAGInstrs.cpp
    ScheduleDAGPrinter.cpp
    ScoreboardHazardRecognizer.cpp
    ShadowStackGCLowering.cpp
    ShrinkWrap.cpp
    SjLjEHPrepare.cpp
    SlotIndexes.cpp
    SpillPlacement.cpp
    SplitKit.cpp
    StackColoring.cpp
    StackMapLivenessAnalysis.cpp
    StackMaps.cpp
    StackProtector.cpp
    StackSlotColoring.cpp
    SwiftErrorValueTracking.cpp
    SwitchLoweringUtils.cpp
    TailDuplication.cpp
    TailDuplicator.cpp
    TargetFrameLoweringImpl.cpp
    TargetInstrInfo.cpp
    TargetLoweringBase.cpp
    TargetLoweringObjectFileImpl.cpp
    TargetOptionsImpl.cpp
    TargetPassConfig.cpp
    TargetRegisterInfo.cpp
    TargetSchedule.cpp
    TargetSubtargetInfo.cpp
    TwoAddressInstructionPass.cpp
    TypePromotion.cpp
    UnreachableBlockElim.cpp
    ValueTypes.cpp
    VirtRegMap.cpp
    WasmEHPrepare.cpp
    WinEHPrepare.cpp
    XRayInstrumentation.cpp
)

END()
"""Poison a child environment and verify normal play removes diagnostic state."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

program=Path(sys.argv[1]).resolve()
names=['WKC2_SAMPLE_PROFILE','WKC2_PROGRESS','WKC2_THREADS','SPU_INTERP_PROFILE',
       'SPU_FALLBACK_STRICT','SPU_CHHIST','SPU_LS_WATCH','SPURS_JOB_TRACE_FP',
       'RSX_ENGINE_AUDIT','RSX_ENGINE_DRAW_CAPTURE','RSX_ENGINE_SNAP_DIR',
       'RSX_DRAW_CSV','RSX_PRESENT_MEASURE','RSX_SCHEDULE_DIAG',
       'LD_FRAME_DUMP','LD_HLSL_DUMP','D3D12_DBG','PERF',
       'PAD_FILE','WKC2_OSK_INPUT_FILE','WKC_NATIVE_SKINNING_TRACE',
       'WKC_NATIVE17_CAPTURE','WKC_NATIVE_SKIN_STREAM','WKC_NATIVE_SKIN_STREAM_VERIFY',
       'WKC_NATIVE_FUTURE_DIAGNOSTIC','WKC_NATIVE_GEOMETRY_DIAGNOSTICS',
       'WKC_NATIVE_GEOMETRY_PARALLEL_PREPARE','WKC_GEOM_PIPELINE_VERIFY']
with tempfile.TemporaryDirectory(prefix='wkc2-clean-play-') as folder:
    env=dict(os.environ,WKC2_PLAYER_MODE='1',WKC2_DISCARD_LOGS='0',PS3_VERBOSE='1')
    for name in names:env[name]=str(Path(folder)/name)
    if os.name=='nt':
        for name in ('wkc2_sample_profile','RsX_ENGINE_AUDIT','ld_frame_dump','wkc_native_skin_stream_verify'):
            env[name]=str(Path(folder)/name)
    # Media-path value is preserved but never loaded by this standalone check.
    env['PS3RECOMP_FFMPEG_AVUTIL']='clean-play-media-sentinel'
    # This test uses two child modes: sanitize explicitly, then query resulting
    # state. No game, output directories, input injection or profiler is started.
    for native_job in ('0','1'):
        env['WKC_NATIVE_GEOMETRY_JOB']=native_job;env['WKC_GEOM_PIPELINE']=native_job
        result=subprocess.run([str(program),'--test-play-environment'],env=env,capture_output=True,text=True,timeout=10)
        assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
        assert 'clean-play environment verified' in result.stdout
        assert 'native-job-mode='+native_job in result.stdout
        assert 'pipeline='+native_job in result.stdout
    assert not list(Path(folder).iterdir()),'A diagnostic created output in clean-play mode'
    research=dict(env,WKC2_PLAYER_MODE='0')
    control=subprocess.run([str(program),'--test-play-environment'],env=research,capture_output=True,text=True,timeout=10)
    assert control.returncode==1,'Explicit research environment was unexpectedly sanitized'
print('Clean-play startup clears inherited profiling, capture, trace, input and future namespace switches')

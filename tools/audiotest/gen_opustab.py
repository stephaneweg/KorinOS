"""gen_opustab.py - tables du decodeur Opus d'AudioKit (src/kits/audiokit/ak_opustab.inc)

    python -I tools/audiotest/gen_opustab.py [dossier de libopus]

Les tables de la norme (RFC 6716 : CELT et SILK) sont lues dans les sources
de libopus (celles de la RFC), ou elles sont recopiees ; aucun code n'en est
repris. Le module expose aussi load() (dictionnaire des tables) pour les
essais.
"""
import os
import re
import struct
import sys

OPUS = os.path.join(os.environ.get('ONYX', 'build/onyx'), 'third_party/opus-1.5.2')
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def _src(rel, base):
    return open(os.path.join(base, rel), encoding='utf-8', errors='replace').read()


def _strip(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', t)


def _array(text, name, conv=int):
    t = _strip(text)
    m = re.search(r'\b' + re.escape(name) + r'\s*(\[[^\]]*\]\s*)+=\s*\{', t)
    if not m:
        raise KeyError(name)
    i = m.end()
    depth = 1
    j = i
    while depth:
        if t[j] == '{':
            depth += 1
        elif t[j] == '}':
            depth -= 1
        j += 1
    body = t[i:j - 1]
    vals = re.findall(r'-?\d+\.?\d*(?:[eE][-+]?\d+)?[fF]?', body)
    out = []
    for v in vals:
        v = v.rstrip('fF')
        out.append(conv(float(v)) if conv is float else int(v))
    return out


def load(base=OPUS):
    c = lambda f: _src(os.path.join('celt', f), base)  # noqa: E731
    T = {}
    modes = c('modes.c')
    T['eband5ms'] = _array(modes, 'eband5ms')
    T['band_allocation'] = _array(modes, 'band_allocation')
    sm = c('static_modes_float.h')
    T['window120'] = _array(sm, 'window120', float)
    T['logN400'] = _array(sm, 'logN400')
    T['cache_index50'] = _array(sm, 'cache_index50')
    T['cache_bits50'] = _array(sm, 'cache_bits50')
    T['cache_caps50'] = _array(sm, 'cache_caps50')
    qb = c('quant_bands.c')
    T['e_prob_model'] = _array(qb, 'e_prob_model')
    em = qb[qb.index('#else', qb.index('eMeans')):]
    T['eMeans'] = _array(em, 'eMeans', float)
    T['LOG2_FRAC_TABLE'] = _array(c('rate.c'), 'LOG2_FRAC_TABLE')
    # SILK
    k = lambda f: _src(os.path.join('silk', f), base)  # noqa: E731
    SILK = {
        'tables_other.c': ['silk_stereo_pred_quant_Q13', 'silk_stereo_pred_joint_iCDF',
                           'silk_stereo_only_code_mid_iCDF', 'silk_LBRR_flags_2_iCDF', 'silk_LBRR_flags_3_iCDF',
                           'silk_lsb_iCDF', 'silk_LTPscale_iCDF', 'silk_type_offset_VAD_iCDF',
                           'silk_type_offset_no_VAD_iCDF', 'silk_NLSF_interpolation_factor_iCDF',
                           'silk_LTPScales_table_Q14', 'silk_uniform3_iCDF', 'silk_uniform4_iCDF',
                           'silk_uniform5_iCDF', 'silk_uniform6_iCDF', 'silk_uniform8_iCDF', 'silk_NLSF_EXT_iCDF'],
        'tables_gain.c': ['silk_gain_iCDF', 'silk_delta_gain_iCDF'],
        'tables_LTP.c': ['silk_LTP_per_index_iCDF', 'silk_LTP_gain_iCDF_0', 'silk_LTP_gain_iCDF_1',
                         'silk_LTP_gain_iCDF_2', 'silk_LTP_gain_vq_0', 'silk_LTP_gain_vq_1', 'silk_LTP_gain_vq_2'],
        'tables_NLSF_CB_NB_MB.c': ['silk_NLSF_CB1_NB_MB_Q8', 'silk_NLSF_CB1_Wght_Q9', 'silk_NLSF_CB1_iCDF_NB_MB',
                                   'silk_NLSF_CB2_SELECT_NB_MB', 'silk_NLSF_CB2_iCDF_NB_MB', 'silk_NLSF_PRED_NB_MB_Q8',
                                   'silk_NLSF_DELTA_MIN_NB_MB_Q15'],
        'tables_NLSF_CB_WB.c': ['silk_NLSF_CB1_WB_Q8', 'silk_NLSF_CB1_WB_Wght_Q9', 'silk_NLSF_CB1_iCDF_WB',
                                'silk_NLSF_CB2_SELECT_WB', 'silk_NLSF_CB2_iCDF_WB', 'silk_NLSF_PRED_WB_Q8',
                                'silk_NLSF_DELTA_MIN_WB_Q15'],
        'tables_pitch_lag.c': ['silk_pitch_lag_iCDF', 'silk_pitch_delta_iCDF', 'silk_pitch_contour_iCDF',
                               'silk_pitch_contour_NB_iCDF', 'silk_pitch_contour_10_ms_iCDF',
                               'silk_pitch_contour_10_ms_NB_iCDF'],
        'tables_pulses_per_block.c': ['silk_pulses_per_block_iCDF', 'silk_rate_levels_iCDF', 'silk_shell_code_table0',
                                      'silk_shell_code_table1', 'silk_shell_code_table2', 'silk_shell_code_table3',
                                      'silk_shell_code_table_offsets', 'silk_sign_iCDF'],
        'table_LSF_cos.c': ['silk_LSFCosTab_FIX_Q12'],
        'pitch_est_tables.c': ['silk_CB_lags_stage2_10_ms', 'silk_CB_lags_stage3_10_ms', 'silk_CB_lags_stage2',
                               'silk_CB_lags_stage3'],
        'resampler_rom.c': ['silk_resampler_frac_FIR_12'],
    }
    for f, names in SILK.items():
        t = k(f)
        for n in names:
            T[n] = _array(t, n)
    T['silk_Quantization_Offsets_Q10'] = [100, 240, 32, 100]  # (UVL, UVH), (VL, VH) : define.h
    T['silk_resampler_up2_hq'] = [1746, 14986, 39083 - 65536, 6854, 25769, 55542 - 65536]
    assert len(T['silk_LSFCosTab_FIX_Q12']) == 129 and len(T['silk_pulses_per_block_iCDF']) == 180
    assert len(T['silk_NLSF_CB1_WB_Q8']) == 512 and len(T['silk_CB_lags_stage3']) == 4 * 34
    assert len(T['eband5ms']) == 22 and len(T['band_allocation']) == 11 * 21
    assert len(T['window120']) == 120 and len(T['e_prob_model']) == 4 * 2 * 42
    assert len(T['cache_bits50']) == 392 and len(T['cache_caps50']) == 168
    return T


def f32(v):
    return '0x%08X' % struct.unpack('<I', struct.pack('<f', v))[0]


def emit(lines, label, vals, kind, per=16, fmt=str):
    lines.append(label + ':')
    for i in range(0, len(vals), per):
        lines.append('\t\t%s\t%s' % (kind, ', '.join(fmt(v) for v in vals[i:i + per])))


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else OPUS
    T = load(base)
    L = ['; ' + '=' * 76,
         ';  kits/audiokit/ak_opustab.inc - tables du decodeur Opus (RFC 6716 : CELT, SILK)',
         ';  ENGENDRE par tools/audiotest/gen_opustab.py (donnees de la norme)',
         '; ' + '=' * 76, '', '\t\talign\t4']
    emit(L, 'ce_window', T['window120'], 'dw', 8, f32)
    emit(L, 'ce_emeans', T['eMeans'], 'dw', 8, f32)
    emit(L, 'ce_eband', T['eband5ms'], 'dh', 22)
    emit(L, 'ce_logn', T['logN400'], 'dh', 21)
    emit(L, 'ce_cindex', T['cache_index50'], 'dh', 15)
    emit(L, 'ce_balloc', T['band_allocation'], 'db', 21)
    emit(L, 'ce_cbits', T['cache_bits50'], 'db', 16)
    emit(L, 'ce_ccaps', T['cache_caps50'], 'db', 21)
    emit(L, 'ce_eprob', T['e_prob_model'], 'db', 21)
    emit(L, 'ce_log2frac', T['LOG2_FRAC_TABLE'], 'db', 24)
    # SILK : octets, puis mots de 16 bits
    B8 = [('sk_pred_joint', 'silk_stereo_pred_joint_iCDF'), ('sk_only_mid', 'silk_stereo_only_code_mid_iCDF'),
          ('sk_lbrr2', 'silk_LBRR_flags_2_iCDF'), ('sk_lbrr3', 'silk_LBRR_flags_3_iCDF'), ('sk_lsb', 'silk_lsb_iCDF'),
          ('sk_ltpscale_icdf', 'silk_LTPscale_iCDF'), ('sk_type_vad', 'silk_type_offset_VAD_iCDF'),
          ('sk_type_novad', 'silk_type_offset_no_VAD_iCDF'), ('sk_interp', 'silk_NLSF_interpolation_factor_iCDF'),
          ('sk_unif3', 'silk_uniform3_iCDF'), ('sk_unif4', 'silk_uniform4_iCDF'), ('sk_unif5', 'silk_uniform5_iCDF'),
          ('sk_unif6', 'silk_uniform6_iCDF'), ('sk_unif8', 'silk_uniform8_iCDF'), ('sk_nlsf_ext', 'silk_NLSF_EXT_iCDF'),
          ('sk_gain_icdf', 'silk_gain_iCDF'), ('sk_dgain', 'silk_delta_gain_iCDF'),
          ('sk_per_icdf', 'silk_LTP_per_index_iCDF'), ('sk_ltpg0', 'silk_LTP_gain_iCDF_0'),
          ('sk_ltpg1', 'silk_LTP_gain_iCDF_1'), ('sk_ltpg2', 'silk_LTP_gain_iCDF_2'),
          ('sk_ltpvq0', 'silk_LTP_gain_vq_0'), ('sk_ltpvq1', 'silk_LTP_gain_vq_1'), ('sk_ltpvq2', 'silk_LTP_gain_vq_2'),
          ('sk_cb1_nb', 'silk_NLSF_CB1_NB_MB_Q8'), ('sk_cb1i_nb', 'silk_NLSF_CB1_iCDF_NB_MB'),
          ('sk_sel_nb', 'silk_NLSF_CB2_SELECT_NB_MB'), ('sk_cb2i_nb', 'silk_NLSF_CB2_iCDF_NB_MB'),
          ('sk_pred_nb', 'silk_NLSF_PRED_NB_MB_Q8'),
          ('sk_cb1_wb', 'silk_NLSF_CB1_WB_Q8'), ('sk_cb1i_wb', 'silk_NLSF_CB1_iCDF_WB'),
          ('sk_sel_wb', 'silk_NLSF_CB2_SELECT_WB'), ('sk_cb2i_wb', 'silk_NLSF_CB2_iCDF_WB'),
          ('sk_pred_wb', 'silk_NLSF_PRED_WB_Q8'),
          ('sk_lag_icdf', 'silk_pitch_lag_iCDF'), ('sk_dlag_icdf', 'silk_pitch_delta_iCDF'),
          ('sk_cont', 'silk_pitch_contour_iCDF'), ('sk_cont_nb', 'silk_pitch_contour_NB_iCDF'),
          ('sk_cont10', 'silk_pitch_contour_10_ms_iCDF'), ('sk_cont10_nb', 'silk_pitch_contour_10_ms_NB_iCDF'),
          ('sk_ppb', 'silk_pulses_per_block_iCDF'), ('sk_rate', 'silk_rate_levels_iCDF'),
          ('sk_shell0', 'silk_shell_code_table0'), ('sk_shell1', 'silk_shell_code_table1'),
          ('sk_shell2', 'silk_shell_code_table2'), ('sk_shell3', 'silk_shell_code_table3'),
          ('sk_shell_off', 'silk_shell_code_table_offsets'), ('sk_sign', 'silk_sign_iCDF'),
          ('sk_lags2_10', 'silk_CB_lags_stage2_10_ms'), ('sk_lags3_10', 'silk_CB_lags_stage3_10_ms'),
          ('sk_lags2', 'silk_CB_lags_stage2'), ('sk_lags3', 'silk_CB_lags_stage3')]
    for lab, name in B8:
        emit(L, lab, T[name], 'db', 16)
    L.append(chr(9) + chr(9) + 'align' + chr(9) + '2')
    H16 = [('sk_pred_q13', 'silk_stereo_pred_quant_Q13'), ('sk_ltpscales', 'silk_LTPScales_table_Q14'),
           ('sk_wght_nb', 'silk_NLSF_CB1_Wght_Q9'), ('sk_dmin_nb', 'silk_NLSF_DELTA_MIN_NB_MB_Q15'),
           ('sk_wght_wb', 'silk_NLSF_CB1_WB_Wght_Q9'), ('sk_dmin_wb', 'silk_NLSF_DELTA_MIN_WB_Q15'),
           ('sk_cos', 'silk_LSFCosTab_FIX_Q12'), ('sk_qoff', 'silk_Quantization_Offsets_Q10'),
           ('sk_fir12', 'silk_resampler_frac_FIR_12'), ('sk_up2c', 'silk_resampler_up2_hq')]
    for lab, name in H16:
        emit(L, lab, T[name], 'dh', 16)
    L += ['\t\talign\t4', '']
    open(os.path.join(ROOT, 'src', 'kits', 'audiokit', 'ak_opustab.inc'), 'w', encoding='utf-8',
         newline='\n').write('\n'.join(L))
    print('ak_opustab.inc :', len(L), 'lignes')


if __name__ == '__main__':
    main()

/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2024 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOSoundOutputTask.h"

#include "GOSoundThread.h"
#include "sound/GOSoundReverb.h"
#include "threading/GOMutexLocker.h"

GOSoundOutputTask::GOSoundOutputTask(
  unsigned channels,
  std::vector<float> scale_factors,
  unsigned samples_per_buffer)
  : GOSoundBufferItem(samples_per_buffer, channels),
    m_ScaleFactors(scale_factors),
    m_Outputs(),
    m_OutputCount(0),
    m_MeterInfo(channels),
    m_Reverb(0),
    m_Done(false) {
  m_Reverb = new GOSoundReverb(m_Channels);
}

GOSoundOutputTask::~GOSoundOutputTask() {
  if (m_Reverb)
    delete m_Reverb;
}

void GOSoundOutputTask::SetOutputs(std::vector<GOSoundBufferItem *> outputs) {
  m_Outputs = outputs;
  m_OutputCount = m_Outputs.size() * 2;
}

void GOSoundOutputTask::Run(GOSoundThread *pThread) {
  if (m_Done.load())
    return;
  GOMutexLocker locker(m_Mutex, false, "GOSoundOutputTask::Run", pThread);

  if (m_Done.load() || !locker.IsLocked())
    return;

  /* initialise the output buffer */
  std::fill(m_Buffer, m_Buffer + m_SamplesPerBuffer * m_Channels, 0.0f);

  for (unsigned i = 0; i < m_Channels; i++) {
    for (unsigned j = 0; j < m_OutputCount; j++) {
      float factor = m_ScaleFactors[i * m_OutputCount + j];
      if (factor == 0)
        continue;

      float *this_buff = m_Outputs[j / 2]->m_Buffer;
      m_Outputs[j / 2]->Finish(m_Stop.load(), pThread);
      if (pThread && pThread->ShouldStop())
        return;

      for (unsigned k = i, l = j % 2; k < m_SamplesPerBuffer * m_Channels;
           k += m_Channels, l += 2)
        m_Buffer[k] += factor * this_buff[l];
    }
  }

  m_Reverb->Process(m_Buffer, m_SamplesPerBuffer);

  /* Clamp the output */
  const float CLAMP_MIN = -1.0f;
  const float CLAMP_MAX = 1.0f;
  for (unsigned k = 0, c = 0; k < m_SamplesPerBuffer * m_Channels; k++) {
    float f = std::min(std::max(m_Buffer[k], CLAMP_MIN), CLAMP_MAX);
    m_Buffer[k] = f;
    if (f > m_MeterInfo[c])
      m_MeterInfo[c] = f;

    c++;
    if (c >= m_Channels)
      c = 0;
  }

  m_Done.store(true);
}

void GOSoundOutputTask::Exec() { Run(); }

void GOSoundOutputTask::Finish(bool stop, GOSoundThread *pThread) {
  if (stop)
    m_Stop.store(true);
  if (!m_Done.load())
    Run(pThread);
}

void GOSoundOutputTask::Clear() {
  m_Reverb->Reset();
  ResetMeterInfo();
}

void GOSoundOutputTask::ResetMeterInfo() {
  GOMutexLocker locker(m_Mutex);
  for (unsigned i = 0; i < m_MeterInfo.size(); i++)
    m_MeterInfo[i] = 0;
}

void GOSoundOutputTask::Reset() {
  GOMutexLocker locker(m_Mutex);
  m_Done.store(false);
  m_Stop.store(false);
}

unsigned GOSoundOutputTask::GetGroup() { return AUDIOOUTPUT; }

unsigned GOSoundOutputTask::GetCost() { return 0; }

bool GOSoundOutputTask::GetRepeat() { return false; }

void GOSoundOutputTask::SetupReverb(GOConfig &settings) {
  m_Reverb->Setup(settings);
}

const std::vector<float> &GOSoundOutputTask::GetMeterInfo() {
  return m_MeterInfo;
}

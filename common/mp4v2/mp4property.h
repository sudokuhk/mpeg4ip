/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is MPEG4IP.
 * 
 * The Initial Developer of the Original Code is Cisco Systems Inc.
 * Portions created by Cisco Systems Inc. are
 * Copyright (C) Cisco Systems Inc. 2001.  All Rights Reserved.
 * 
 * Contributor(s): 
 *		Dave Mackie		dmackie@cisco.com
 */

#ifndef __MP4_PROPERTY_INCLUDED__
#define __MP4_PROPERTY_INCLUDED__

#include <sys/types.h>
#include <stdio.h>
#include <vector>

// forward declaration 
class MP4File;

enum MP4PropertyType {
	Integer8Property,
	Integer16Property,
	Integer32Property,
	Integer64Property,
	FloatProperty,
	StringProperty,
	BytesProperty,
	TableProperty,
	DescriptorProperty,
};

class MP4Property {
public:
	MP4Property(char *name = NULL) {
		SetName(name);
		m_readOnly = false;
		m_implicit = false;
	}

	char *GetName() {
		return m_name;
	}
	void SetName(char* name) {
		MP4Free(m_name);
		if (name) {
			m_name = (char*)MP4Malloc(strlen(name) + 1);
			strcpy(m_name, name);
		} else {
			m_name = NULL;
		}
	}

	virtual MP4PropertyType GetType() = NULL; 

	bool IsReadOnly() {
		return m_readOnly;
	}
	void SetReadOnly(bool value = true) {
		m_readOnly = value;
	}

	bool IsImplicit() {
		return m_implicit;
	}
	void SetImplicit(bool value = true) {
		m_implicit = value;
	}

	virtual u_int32_t GetCount() = NULL;
	virtual void SetCount(u_int32_t count) = NULL;

	virtual void Read(MP4File* pFile, u_int32_t index = 0) = NULL;
	virtual void Write(MP4File* pFile, u_int32_t index = 0) = NULL;
	virtual void Dump(FILE* pFile, u_int32_t index = 0) = NULL;

	virtual MP4Property* FindProperty(char* name) {
		return strcasecmp(m_name, name) ? NULL : this;
	}

protected:
	char* m_name;
	bool m_readOnly;
	bool m_implicit;
};

template <class T> class MP4IntegerProperty : public MP4Property {
public:
	MP4IntegerProperty(char* name)
		: MP4Property(name) {
		SetCount(1);
		m_values[0] = 0;
	}

	MP4PropertyType GetType();

	u_int32_t GetCount() {
		return m_values.size();
	}
	void SetCount(u_int32_t count) {
		m_values.resize(count);
	}

	T GetValue(u_int32_t index = 0) {
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"IntegerProperty::GetValue");
		}
		return m_values[index];
	}

	void SetValue(T value, u_int32_t index = 0) {
		if (m_readOnly) {
			throw new MP4Error(EACCES, "property is read-only", m_name);
		}
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"IntegerProperty::SetValue");
		}
		m_values[index] = value;
	}

	void Read(MP4File* pFile, u_int32_t index = 0);
	void Write(MP4File* pFile, u_int32_t index = 0);
	void Dump(FILE* pFile, u_int32_t index = 0);

protected:
	vector<T> m_values;
};

class MP4BitfieldProperty : public MP4IntegerProperty<u_int64_t> {
public:
	MP4BitfieldProperty(char* name, u_int8_t numBits)
		: MP4IntegerProperty<u_int64_t>(name) {
		m_numBits = numBits;
	}

	u_int8_t GetNumBits() {
		return m_numBits;
	}
	void SetNumBits(u_int8_t numBits) {
		m_numBits = numBits;
	}

	void Read(MP4File* pFile, u_int32_t index = 0);
	void Write(MP4File* pFile, u_int32_t index = 0);

protected:
	u_int8_t m_numBits;
};

class MP4StringProperty : public MP4Property {
public:
	MP4StringProperty(char* name, bool usePascalFormat = false)
		: MP4Property(name) {
		SetCount(1);
		m_values[0] = NULL;
		m_usePascalFormat = usePascalFormat;
	}
	~MP4StringProperty() {
		u_int32_t count = GetCount();
		for (u_int32_t i = 0; i < count; i++) {
			MP4Free(m_values[i]);
		}
	}

	MP4PropertyType GetType() {
		return StringProperty;
	}

	u_int32_t GetCount() {
		return m_values.size();
	}
	void SetCount(u_int32_t count) {
		m_values.resize(count);
	}

	const char* GetValue(u_int32_t index = 0) {
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"StringProperty::GetValue");
		}
		return m_values[index];
	}

	void SetValue(char* value, u_int32_t index = 0) {
		if (m_readOnly) {
			throw new MP4Error(EACCES, "property is read-only", m_name);
		}
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"StringProperty::SetValue");
		}
		MP4Free(m_values[index]);
		if (value) {
			m_values[index] = (char*)MP4Malloc(strlen(value) + 1);
			strcpy(m_values[index], value);
		} else {
			m_values[index] = NULL;
		}
	}

	void Read(MP4File* pFile, u_int32_t index = 0);
	void Write(MP4File* pFile, u_int32_t index = 0);
	void Dump(FILE* pFile, u_int32_t index = 0);

protected:
	vector<char*> m_values;
	bool m_usePascalFormat;
};

class MP4BytesProperty : public MP4Property {
public:
	MP4BytesProperty(char* name, u_int32_t valueSize = 0)
		: MP4Property(name) {
		SetCount(1);
		m_values[0] = NULL;
		m_valueSizes[0] = valueSize;
	}
	~MP4BytesProperty() {
		u_int32_t count = GetCount();
		for (u_int32_t i = 0; i < count; i++) {
			MP4Free(m_values[i]);
		}
	}

	MP4PropertyType GetType() {
		return BytesProperty;
	}

	u_int32_t GetCount() {
		return m_values.size();
	}
	void SetCount(u_int32_t count) {
		m_values.resize(count);
	}

	void GetValue(u_int8_t** ppValue, u_int32_t* pValueSize, u_int32_t index = 0) {
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"BytesProperty::GetValue");
		}
		*ppValue = m_values[index];
		*pValueSize = m_valueSizes[index];
	}

	void SetValue(u_int8_t* pValue, u_int32_t valueSize, u_int32_t index = 0) {
		if (m_readOnly) {
			throw new MP4Error(EACCES, "property is read-only", m_name);
		}
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"BytesProperty::SetValue");
		}
		MP4Free(m_values[index]);
		if (pValue) {
			m_values[index] = (u_int8_t*)MP4Malloc(valueSize);
			memcpy(m_values[index], pValue, valueSize);
			m_valueSizes[index] = valueSize;
		} else {
			m_values[index] = NULL;
			m_valueSizes[index] = 0;
		}
	}

	u_int32_t GetValueSize(u_int32_t valueSize, u_int32_t index = 0) {
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"BytesProperty::GetValueSize");
		}
		return m_valueSizes[index];
	}
	void SetValueSize(u_int32_t valueSize, u_int32_t index = 0) {
		if (index >= m_values.size()) {
			throw new MP4Error(ERANGE, "index out of range",
				"BytesProperty::SetValueSize");
		}
		if (m_values[index] != NULL) {
			m_values[index] = (u_int8_t*)MP4Realloc(m_values[index], valueSize);
		}
		m_valueSizes[index] = valueSize;
	}

	void Read(MP4File* pFile, u_int32_t index = 0);
	void Write(MP4File* pFile, u_int32_t index = 0);
	void Dump(FILE* pFile, u_int32_t index = 0);

protected:
	vector<u_int32_t> m_valueSizes;
	vector<u_int8_t*> m_values;
};

class MP4TableProperty : public MP4Property {
public:
	MP4TableProperty(char* name, MP4IntegerProperty<u_int32_t>* pCountProperty)
		: MP4Property(name) {
		m_pCountProperty = pCountProperty;
		m_pCountProperty->SetReadOnly();
	}

	MP4PropertyType GetType() {
		return TableProperty;
	}

	void AddProperty(MP4Property* pProperty) {
		ASSERT(FindContainedProperty(pProperty->GetName()) == NULL);
		m_pProperties.insert(m_pProperties.end(), pProperty);
	}

	u_int32_t GetCount() {
		return m_pCountProperty->GetValue();
	}
	void SetCount(u_int32_t count) {
		// TBD SetCount on table
		// Should we vector this over to our count property?
		ASSERT(FALSE);
	}

	void Read(MP4File* pFile, u_int32_t index = 0);
	void Write(MP4File* pFile, u_int32_t index = 0);
	void Dump(FILE* pFile, u_int32_t index = 0);

	MP4Property* FindProperty(char* name);

protected:
	MP4Property* FindContainedProperty(char* name);

protected:
	MP4IntegerProperty<u_int32_t>* m_pCountProperty;
	vector<MP4Property*>	m_pProperties;
};

class MP4DescriptorProperty : public MP4Property {
public:
	MP4DescriptorProperty(u_int8_t tag = 0, char* name = NULL)
		: MP4Property(name) { 
		m_tag = tag;
	}

	MP4PropertyType GetType() {
		return DescriptorProperty;
	}

	u_int8_t GetTag() {
		return m_tag;
	}
	void SetTag(u_int8_t tag) {
		m_tag = tag;
	}

	void AddProperty(MP4Property* pProperty) {
		ASSERT(FindContainedProperty(pProperty->GetName()) == NULL);
		m_pProperties.insert(m_pProperties.end(), pProperty);
	}

	u_int32_t GetCount() {
		if (m_pProperties.size() == 0) {
			return 1;
		}
		return m_pProperties[0]->GetCount();
	}
	void SetCount(u_int32_t count) {
		u_int32_t numProperties = m_pProperties.size();
		for (u_int32_t i = 0; i < numProperties; i++) {
			m_pProperties[i]->SetCount(count);
		}
	}

	void Read(MP4File* pFile, u_int32_t index = 0);
	void Write(MP4File* pFile, u_int32_t index = 0);
	void Dump(FILE* pFile, u_int32_t index = 0);

	MP4Property* FindProperty(char* name);

protected:
	bool PeekTag(MP4File* pFile, u_int8_t desiredTag);
	void ReadHeader(MP4File* pFile);
	void ReadProperties(MP4File* pFile, u_int32_t index = 0,
		u_int32_t startIndex = 0, u_int32_t count = 0xFFFFFFFF);

	MP4Property* FindContainedProperty(char* name);

protected:
	u_int8_t m_tag;
	u_int64_t m_start;
	u_int32_t m_length;

	vector<MP4Property*>	m_pProperties;
};

#endif /* __MP4_PROPERTY_INCLUDED__ */